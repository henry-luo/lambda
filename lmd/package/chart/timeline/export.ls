// snapshot and source export share normalized records; effects use S12.1.1v2.
import c: .common
import schema: .schema
import layout: .layout
import reducer: .state
import story: .story
import present: .present
import dates: .date
import util: lambda.chart.util
import table: lambda.chart.export
import radiant
import resource: .resource
import media: .media
import chart_parse: lambda.chart.parse

let default_fields=["id","display_date","at","from","to","title","subtitle","content","group","subgroup","tags","metadata"]

fn options_for(spec, options) => if (not (options is map)) c.fail("export.options","expected an options map") else schema.config({*:spec.export,*:options},"export")
fn normalize(raw, options, current) {
    let authored=schema.authored(raw);
    let source=c.attrs(current);
    let declared=if (authored.export.snapshot is map) authored.export.snapshot else {};
    let supplied=if (options.snapshot is map) options.snapshot else {};
    if (authored is error) authored else resource.normalize(raw,{*:source,*:declared,*:supplied})
}
fn rich(value, policy) {
    if (value is element or value is fn or value is pn) {
        if (policy=="text" and not (value is fn or value is pn)) c.plain(value)
        else if (policy=="omit") null else c.fail("export.content","rich content requires an explicit text or omit policy")
    } else if (value is map) {
        let record=map([for (key,item in value) (key,rich(item,policy))]);
        let failure=util.first_error([for (key,item in record) item]);
        if (failure is error) failure else record
    } else if (value is array) {
        let values=[for (item in value) rich(item,policy)];
        let failure=util.first_error(values);
        if (failure is error) failure else values
    } else value
}
pub fn snapshot(spec, options = {}, current = null) {
    let supplied=if (options.snapshot is map) {*:c.attrs(current),*:options.snapshot} else current;
    let base=reducer.reconcile(spec,{*:reducer.initial(spec),*:c.attrs(supplied)});
    let frozen=if (options.time_ms!=null) story.sample(story.plan(spec,base),options.time_ms) else {};
    let expanded=if (options.expanded==true) spec.events |> ~.id else if (options.expanded==false) [] else base.expanded_events;
    if (frozen is error) frozen else {*:base,*:frozen,story_frame:if (options.time_ms!=null) frozen else base.story_frame,
        sample_time_ms:options.time_ms,expanded_events:expanded,
        window:if (options.scope=="full") layout.extent(spec) else base.window}
}
fn rows_in(spec, options, current) {
    let scope=c.text(c.option(options,"scope","full"));
    if (scope=="full") spec.events
    else if (scope=="selected") spec.events |: contains(current.selected_ids,~.id)
    else if (scope=="current") layout.visible(spec,current).events
    else if (scope=="window") layout.visible(spec,current).events |: layout.in_window(~,current.window)
    else c.fail("export.scope","expected full, current, window, or selected")
}
fn source_event(item, policy) => rich({*:schema.event_record(item),display_date:item.display_date},policy)

pub fn dataset(raw, options = {}, current = null) {
    let spec=normalize(raw,options,current);
    let opts=options_for(spec,options);
    let policy=c.text(c.option(opts,"unsupported","error"));
    let frozen=if (spec is error) spec else if (opts is error) opts else snapshot(spec,opts,current);
    let selected=if (frozen is error) frozen else rows_in(spec,opts,frozen);
    let rows=if (selected is error) [] else [for (item in selected) source_event(item,policy)];
    let details={source:rich(spec.source,policy),scale:rich(spec.scale,policy),groups:rich(spec.groups,policy),eras:rich(spec.eras,policy)};
    let failure=util.first_error([spec,opts,frozen,selected,*rows,*[for (key,value in details) value],
        if (not contains(["error","text","omit"],policy)) c.fail("export.unsupported","expected error, text, or omit")]);
    if (failure is error) failure else {version:1,chart_id:spec.id,*:details,events:rows,
        snapshot:{window:frozen.window,active_id:frozen.active_id,selected_ids:frozen.selected_ids,
            expanded_events:frozen.expanded_events,locale:spec.locale,timezone:spec.scale.timezone,revision:frozen.revision,
            time_ms:c.option(opts,"time_ms",frozen.playback.elapsed),
            resources:[for (key,entry in c.attrs(frozen.resources)) {id:string(key),revision:entry.revision,status:entry.status,url:entry.descriptor.url}]}}
}
fn columns(spec, options) {
    let fields=c.option(options,"fields",default_fields);
    let values=if (fields is array) [for (field in fields) if (field is string or field is symbol)
        {field:c.text(field),heading:c.text(c.option(c.attrs(options.headings),c.text(field),c.message(spec,"export_" ++ c.text(field),c.text(field))))}
        else c.fail("export.fields","expected field names")] else [c.fail("export.fields","expected an ordered field array")];
    let failure=util.first_error(values);
    if (failure is error) failure else if (len(util.unique_vals(values |> ~.heading))!=len(values))
        c.fail("export.headings","duplicate headings") else values
}
fn table_value(item, field, options) {
    let value=chart_parse.channel_value({field:field},item);
    if (contains(["at","from","to"],field) and value!=null and options.date_formatter is fn)
        options.date_formatter(value,{event:item,field:field})
    else if (value is map or value is array) format(value,'json') else value
}
pub fn data(raw, kind = "json", options = {}, current = null) {
    let spec=normalize(raw,options,current);
    let opts=options_for(spec,options);
    let payload=dataset(raw,opts,current);
    let cols=columns(spec,opts);
    let rows=if (payload is error or cols is error) [] else [for (item in payload.events,
        let row=map([for (column in cols) (column.field,table_value(item,column.field,opts))]),
        let failure=util.first_error([for (key,value in row) value])) if (failure is error) failure else row];
    let failure=util.first_error([spec,opts,payload,cols,*rows]);
    if (failure is error) failure else if (kind=="json") payload
    else if (kind=="csv") table.csv(cols,rows)
    else if (kind=="xlsx") table.xlsx(cols,rows,{*:opts,sheet_name:c.option(opts,"sheet_name",c.message(spec,"export_sheet","Timeline"))})
    else if (kind=="html" or kind=="pdf") table.html_table(cols,rows,{caption:c.option(opts,"caption",c.plain(spec.title))})
    else c.fail("export.format","unknown data format")
}
fn static_event(item, options, frozen) {
    let records=[for (index,record in item.media) media.snapshot(record,item.id,index,options,frozen)];
    let failure=util.first_error(records);
    if (failure is error) failure else {*:item,media:records |: ~!=null}
}
fn static_source(spec, options, frozen) {
    let events=[for (item in spec.events) static_event(item,options,frozen)];
    let intro=if (spec.intro!=null) static_event(spec.intro,options,frozen) else null;
    let failure=util.first_error([intro,*events]);
    if (failure is error) failure else {*:spec,events:events,intro:intro,
        cluster:if (options.scope=="full" and options.clusters!=true) {*:spec.cluster,enabled:false} else spec.cluster}
}
pub fn document(raw, kind = "html", viewport = null, options = {}, current = null) {
    let spec=normalize(raw,options,current);
    let opts=options_for(spec,options);
    let frozen=if (spec is error) spec else if (opts is error) opts else snapshot(spec,opts,current);
    let source=if (frozen is error) frozen else static_source(spec,opts,frozen);
    let plan=if (source is error) source else layout.normalized(source,viewport,frozen);
    if (plan is error) plan else if (contains(["html","svg","png","jpeg","pdf"],kind)) {
        let presentation=if (opts.table==true) data(raw,"html",opts,frozen) else present.html(source,viewport,frozen);
        let source=if (presentation is error) presentation else <html lang:c.option(spec,"locale","en"),dir:spec.direction,
            <head <meta charset:"utf-8"> <title c.plain(spec.title)>
                <style "body{margin:0;background:white}.lambda-export-table{border-collapse:separate;border-spacing:0;width:100%}.lambda-export-table th,.lambda-export-table td{border:1px solid #bbb;padding:6px;text-align:start;vertical-align:top}.lambda-export-table thead{display:table-header-group}.lambda-export-table tr{break-inside:avoid}.lambda-timeline-toolbar,.lambda-timeline-navigator{display:none}@media print{.lambda-timeline-chart{width:100%}.lambda-timeline-overlay-card{break-inside:avoid}}">
            >
            <body 'data-timeline-snapshot':format(dataset(raw,{*:opts,unsupported:"omit"},frozen).snapshot,'json'),presentation>
        >;
        if (source is error or kind!="svg") source else {
            // Radiant's vector exporter preserves the same rich card tree and images.
            let box=document_box(source,{width:plan.width,height:plan.height},opts);
            let encoded=if (box is error) box else radiant.render_svg(format(source,'html'),int(ceil(box.width)),int(ceil(box.height)));
            let parsed=if (encoded is error) encoded else if (encoded==null) c.fail("export.svg","vector presentation failed") else parse(encoded,'xml') ^ { ^ };
            let root=if (parsed is error) parsed else (content(parsed) |: ~ is element and name(~)=='svg')[0];
            if (root is error) root else if (root==null) c.fail("export.svg","vector output has no SVG root")
            else <svg *:c.attrs(root),role:"img",'aria-label':c.plain(c.option(spec,"label",spec.title)),
                *[<title c.plain(c.option(spec,"label",spec.title))>,<desc c.plain(spec.description)>,*content(root)]>
        }
    } else c.fail("export.format","unknown document format")
}
fn document_box(document, viewport, options) map | error {
    if (options.scope!="full" and options.expanded!=true) viewport else {
        let measured=radiant.measure_html(format(document,'html'),int(ceil(viewport.width)),int(ceil(viewport.height)));
        if (measured==null or len(measured)!=1 or measured[0]==null) c.fail("export.measure","document measurement failed")
        else {width:max(viewport.width,measured[0].width),height:max(viewport.height,measured[0].height)}
    }
}

pub pn save(raw, destination, kind = null, viewport = null, options = {}, current = null) {
    let spec=normalize(raw,options,current)^;
    let opts=options_for(spec,options)^;
    if (not (destination is string) or destination=="") raise c.fail("export.filename","expected a destination path");
    let extension=lower(split(destination,".")[len(split(destination,"."))-1]);
    let selected=c.text(if (kind!=null) kind else c.option(opts,"format",extension));
    let format_name=if (selected=="jpg") "jpeg" else selected;
    if (contains(["json","csv","xlsx"],format_name)) {
        let value=data(raw,format_name,opts,current)^;
        output(value,destination,{format:if (format_name=="xlsx") "zip" else if (format_name=="csv") "text" else "json",deterministic:true})^;
        destination
    } else {
        let value=document(raw,format_name,viewport,opts,current)^;
        if (format_name=="html" or format_name=="svg") {
            output(value,destination,{format:if (format_name=="svg") "xml" else "html"})^;
            destination
        } else {
            let box=document_box(value,layout.dimensions(spec,viewport)^,opts)^;
            let done=radiant.export_document(format(value,'html')^,destination,
                {format:format_name,width:ceil(box.width),height:ceil(box.height),scale:c.option(opts,"scale",1),
                    device_scale:c.option(opts,"device_scale",1),quality:c.option(opts,"quality",85),
                    pagination:opts.pagination==true or opts.pagination.enabled==true,
                    local_resources:opts.resources=="local",base_url:opts.base_url});
            if (done==true) destination else raise c.fail("export",c.text(done))
        }
    }
}
