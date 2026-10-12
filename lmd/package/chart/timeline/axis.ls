// chronology ticks and measured labels follow the shared timeline coordinate.
import c: .common
import dates: .date
import util: lambda.chart.util
import calendar: lambda.chart.calendar
import zones: lambda.chart.timezone
import text: lambda.chart.text
import collision: lambda.chart.collision
import coordinate: .coordinate
import time_map: .time_map

fn tick_label(value, spec, axis, native, domain) {
    let record=dates.from_coordinate(value,spec.scale);
    let pattern=c.option(axis,"format",spec.scale.format);
    if (record is error) record else if (axis.formatter is fn) axis.formatter(record,{locale:spec.locale})
        else if (pattern!=null or spec.scale.formatter is fn) {
            let formatted=dates.from_coordinate(value,{*:spec.scale,*:(if (pattern!=null) {format:pattern} else {})});
            if (formatted is error) formatted else formatted.label
        }
        else if (native) dates.label(record,{*:spec.scale,format:if (domain.to-domain.from>63072000000) "YYYY" else "MMM DD"})
        else if (spec.scale.kind=="calendar") dates.label({*:record,precision:"year"},spec.scale)
        else record.label
}

fn ticks(spec, domain, axis) {
    let count=c.option(axis,"tick_count",8);
    let native=spec.scale.kind=="calendar" and domain.from>=dates.days(1)*i64(86400000) and domain.to<=dates.days(9999)*i64(86400000);
    let chosen=calendar.interval(if (axis.tick_unit!=null) {interval:c.text(axis.tick_unit),step:c.option(axis,"tick_step",1)} else count,domain.to-domain.from);
    let values=if (native) calendar.ticks(domain.from,domain.to,
        {interval:chosen.unit,step:chosen.step,week_start:c.option(spec.scale,"week_start",1)},spec.scale.timezone)
        else if (spec.scale.kind=="calendar") {
            let first=dates.civil(dates.floor_div(i64(domain.from),i64(86400000))).year;
            let final_year=dates.civil(dates.floor_div(i64(domain.to),i64(86400000))).year;
            let step=max(1,int(util.nice_num(max(1,final_year-first)/max(1,count),true)));
            [for (index in 0 to int(ceil(float(final_year-first)/step)),
                let year=dates.floor_div(first,step)*step+index*step,
                let at=dates.days(year)*i64(86400000) where at>=domain.from and at<=domain.to) at]
        } else [for (offset in util.nice_ticks(0,float(domain.to-domain.from),count),
            let at=domain.from+decimal(offset) where at>=domain.from and at<=domain.to) at];
    let labelled=if (values is error) [] else [for (value in values) {value:value,label:tick_label(value,spec,axis,native,domain)}];
    let failure=if (values is error) values else util.first_error(labelled |> ~.label);
    if (failure is error) failure else labelled
}

pub fn week_number(record, start = 1) {
    let day=dates.days(record.year,record.month,record.day);
    let first=dates.days(record.year,1,4);
    let weekday=(day+4)%7;
    let aligned=day-((weekday-start+7)%7);
    let anchor=first-(((first+4)%7-start+7)%7);
    if (aligned<anchor) week_number({year:record.year-1,month:12,day:31},start)
    else {
        let next=dates.days(record.year+1,1,4);
        let boundary=next-(((next+4)%7-start+7)%7);
        if (aligned>=boundary) 1 else (aligned-anchor) div 7+1
    }
}
fn row(tick, model, path, font, side, rotation, index, count) {
    let projected=coordinate.project(path,tick.fraction);
    let separation=18+c.option(model,"_offset",0);
    let x=projected.point[0]+projected.normal[0]*separation*side;
    let y=projected.point[1]+projected.normal[1]*separation*side;
    let angle=if (model.rotation!=null) model.rotation else if (model._label_angle=="tangent") projected.angle
        else if (model._label_angle is number) model._label_angle else rotation;
    let metric=text.measure([c.plain(tick.label)],font);
    if (metric is error) metric else {*:tick,*:projected,index:index,side:side,label:c.plain(tick.label),x:x,y:y,angle:angle,
        bounds:text.bounds(metric[0],"middle",angle,x,y),optional:index!=0 and index!=count-1,gap:2.0}
}
fn labels(ticks, model, path, font, side, rotation) => [for (index,tick in ticks) row(tick,model,path,font,side,rotation,index,len(ticks))]
fn inside(bounds, limit) => limit==null or (limit.left==null or bounds.left>=limit.left) and (limit.right==null or bounds.right<=limit.right) and
    (limit.top==null or bounds.top>=limit.top) and (limit.bottom==null or bounds.bottom<=limit.bottom)

fn prepare_axis(spec, domain, temporal, path, font, model, index) {
    let values=ticks(spec,domain,model);
    let visible=if (values is error) values else [for (tick in values where not time_map.project(temporal,tick.value).hidden)
        {*:tick,fraction:time_map.fraction(temporal,tick.value),label:if (model.week_numbers==true and spec.scale.kind=="calendar")
            c.message(spec,"week_number","W{number}",{number:week_number(dates.from_coordinate(tick.value,spec.scale),c.option(spec.scale,"week_start",1))}) else tick.label}];
    let minor=if (model.minor!=true) [] else ticks(spec,domain,{*:model,tick_count:c.option(model,"tick_count",8)*5,tick_unit:null,tick_step:1});
    let side_values=if (model.position=="both") [-1.0,1.0] else [if (model.position=="start") -1.0 else 1.0];
    let candidates=if (visible is error) visible else [for (side in side_values)
        for (entry in labels(visible,model,path,font,side,0.0)) entry];
    let collision_policy=c.text(c.option(model,"collision","omit"));
    let rows=if (candidates is error) candidates else if (collision_policy=="rotate" and model.rotation==null and
        len(collision.select(candidates))<len(candidates)) [for (side in side_values) for (entry in labels(visible,model,path,font,side,45.0)) entry] else candidates;
    let failure=util.first_error([values,minor,candidates,rows,*c.items(rows)]);
    if (failure is error) failure else {
        let bounded=rows |: inside(~.bounds,model.label_bounds);
        let chosen=if (collision_policy=="none") bounded else collision.select(bounded);
        let midpoint=coordinate.project(path,0.5);
        let title_offset=(44+c.option(model,"_offset",0))*(if (model.position=="start") -1.0 else 1.0);
        {id:c.option(model,"id","axis-" ++ string(index)),config:model,labels:if (model.major==false) [] else chosen,
            omitted:len(rows)-len(chosen),ticks:[for (tick in visible) {*:tick,*:coordinate.project(path,tick.fraction),major:true,sides:side_values}],
            minor:[for (tick in minor where not contains(visible |> ~.value,tick.value) and not time_map.project(temporal,tick.value).hidden)
                {*:tick,*:coordinate.project(path,time_map.fraction(temporal,tick.value)),major:false,sides:side_values}],
            title:if (model.title!=null) c.plain(model.title) else null,
            title_point:[midpoint.point[0]+midpoint.normal[0]*title_offset,midpoint.point[1]+midpoint.normal[1]*title_offset]}
    }
}

pub fn prepare(spec, domain, temporal, path, font, box) {
    let models=if (len(spec.axes)==0) [{}] else spec.axes;
    let prepared=[for (index,model in models where model.position!="none" and
        (model.scale==null or c.text(model.scale)==c.text(c.option(spec,"time_scale","time"))))
        prepare_axis(spec,domain,temporal,path,font,{*:model,_label_angle:spec.coordinate.label_angle,_offset:index*28},index)];
    let failure=util.first_error(prepared);
    if (failure is error) failure else prepared
}

// automatic curved shapes reserve measured guide space around their outer turns.
pub fn gutter(spec, domain, font) {
    if (not contains(["serpentine","spiral"],c.text(spec.coordinate.kind))) 0.0 else {
        let models=if (len(spec.axes)==0) [{}] else spec.axes;
        let labels=[for (model in models where model.position!="none")
            for (tick in ticks(spec,domain,model)) c.plain(tick.label)];
        let metrics=text.measure(labels,font);
        if (metrics is error) metrics else max([0.0,for (metric in metrics) max(metric.width/2.0,metric.height)])+
            18+max(0,len(models)-1)*28+(if (any([for (model in models) model.title!=null])) 26 else 0)
    }
}
