// Native controls produce the same immutable filter action as external callers.
import c: .common
import dates: .date
import util: lambda.chart.util
import dom
import events: lambda.chart.events

fn fields(spec) => if (spec.search.controls==true) ["groups","tags","from","to"] else [for (field in c.items(spec.search.controls) where field!=false) c.text(field)]
fn values(spec, field) => if (field=="groups") [for (group in spec.groups) {id:group.id,label:if (group.title==null) group.id else group.title}]
    else [for (tag in util.unique_vals([for (item in spec.events) for (tag in c.items(item.tags)) tag])) {id:tag,label:tag}]
fn effective(spec, current, field) => c.option(c.attrs(current.filters),field,spec.search[field])
pub fn control_value(spec, current, field, id = null) {
    let selected=effective(spec,current,field);
    if (contains(["groups","tags"],field)) selected==null or contains(selected,id)
    else date_text(spec,selected)
}
fn date_text(spec, value) {
    if (value==null) "" else if (value is string) value else if (spec.scale.kind!="calendar") c.text(value)
    else {
        let parsed=dates.normalize(value,{*:spec.scale,locale:"en-US",formatter:null});
        let precision=parsed.precision;
        let pattern=if (precision=="year") "YYYY" else if (precision=="month") "YYYY-MM" else "YYYY-MM-DD" ++
            (if (contains(["hour","minute","second","millisecond"],precision)) "[T]HH:mm" ++
                (if (contains(["second","millisecond"],precision)) ":ss" else "") ++
                (if (precision=="millisecond") ".SSS" else "") ++ "Z" else "");
        if (parsed is error) "" else dates.label({*:parsed,display_date:null},{format:pattern})
    }
}
pub fn render(spec, current = null) {
    if (spec.search.enabled==false or len(fields(spec))==0) null else
    <div class:"lambda-timeline-filters",
        *[for (field in fields(spec),
            let label=c.message(spec,"filter_" ++ field,if (field=="groups") "Filter groups" else if (field=="tags") "Filter tags" else if (field=="from") "From" else "To"),
            let selected=effective(spec,current,field))
            if (contains(["groups","tags"],field)) <fieldset 'data-timeline-filter-set':field,
                <legend label>;
                *[for (value in values(spec,field)) <label <input type:"checkbox",'data-timeline-filter':field,
                    'data-timeline-filter-value':c.text(value.id),id:"timeline-filter-" ++ util.binding_key([spec.id,field,value.id]),
                    *:(if (selected==null or contains(selected,value.id)) {checked:""} else {})>c.render(value.label)>]>
            else <label label;
                <input type:"text",id:"timeline-filter-" ++ util.binding_key([spec.id,field]),'data-timeline-filter':field,
                    value:date_text(spec,selected),'aria-label':label,placeholder:if (spec.scale.kind=="calendar") "YYYY-MM-DD" else c.text(spec.scale.unit)>>];
        <button type:"button",'data-timeline-action':format({type:"filter",filters:{groups:null,tags:null,from:null,to:null}},'json'),
            c.message(spec,"clear_filters","Clear filters")>
    >
}

pub pn action(node, spec, current) {
    let field=events.attribute(node,"data-timeline-filter");
    let previous=c.attrs(current.filters);
    let selected=effective(spec,current,field);
    let value=if (contains(["groups","tags"],field)) {
        let options=values(spec,field);
        let id=([for (item in options where c.text(item.id)==events.attribute(node,"data-timeline-filter-value")) item])[0].id;
        let old=if (selected==null) options |> ~.id else selected;
        if (id==null) c.fail("filters." ++ field,"unknown filter option")
        else if (dom.get_state(node,"checked")==true) util.unique_vals([*old,id]) else old |: ~!=id
    } else {
        let text=dom.get_state(node,"value");
        let raw=if (text=="") null else if (spec.scale.kind=="calendar") text else decimal(text);
        let normalized=if (raw is error or raw==null) raw else dates.normalize(raw,spec.scale);
        if (normalized is error) normalized else raw
    };
    if (value is error) value else {type:"filter",filters:{*:previous,[field]:value}}
}

// Authored defaults cannot reset a dirty native control when a controlled request is held.
pub pn restore(node, spec, current) {
    let field=events.attribute(node,"data-timeline-filter");
    let option=([for (item in values(spec,field) where c.text(item.id)==events.attribute(node,"data-timeline-filter-value")) item])[0];
    let key=if (contains(["groups","tags"],field)) "checked" else "value";
    let value=control_value(spec,current,field,option.id);
    if (dom.get_state(node,key)!=value) { dom.set_state(node,key,value) }
}
