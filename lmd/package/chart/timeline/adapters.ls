// pure adapters normalize already-loaded records; loading belongs to a procedure.
import c: .common
import dates: .date
import schema: .schema
import util: lambda.chart.util

fn loaded(data, format) {
    if (data is string) {
        parse(data,symbol(format)) ^ { ^ }
    } else data
}
fn sheet_records(data) {
    if (data is array) data
    else if (data.table.rows is array and data.table.cols is array) [for (row in data.table.rows)
        map([for (index,column in data.table.cols)
            (if (column.label!=null and column.label!="") column.label else column.id,
             if (row.c[index].f!=null) row.c[index].f else row.c[index].v)])]
    else if (data.feed.entry is array) [for (entry in data.feed.entry)
        map([for (key,value in entry where starts_with(string(key),"gsx$")) (slice(string(key),4),value["$t"])])]
    else c.fail("convert.sheets","expected published spreadsheet rows or a Google Visualization table")
}
fn spreadsheet(data) {
    let rows=sheet_records(data);
    if (rows is error) rows else {
        let events=[for (row in rows,
            let r=map([for (key,value in c.attrs(row)) (replace(lower(trim(string(key)))," ","_"),value)])) {
            unique_id:c.option(r,"id",r.unique_id),type:r.type,group:r.group,display_date:r.display_date,
            start_date:{year:r.year,month:r.month,day:r.day,display_date:r.display_date,*:clock_fields(r.time)},
            end_date:if (r.end_year!=null and r.end_year!="") {year:r.end_year,month:r.end_month,day:r.end_day,*:clock_fields(r.end_time)} else null,
            text:{headline:r.headline,text:r.text},background:r.background,
            media:{url:r.media,credit:r.media_credit,caption:r.media_caption,thumbnail:r.media_thumbnail,alt:r.media_alt}}];
        {title:(events |: lower(c.text(~.type))=="title")[0],
            events:events |: not contains(["title","era"],lower(c.text(~.type))),eras:events |: lower(c.text(~.type))=="era"}
    }
}
fn clock_fields(value) {
    let fields=if (value is string and value!="") split(value,":") else [];
    map([for (index,key in ["hour","minute","second"] where fields[index]!=null) (key,i64(fields[index]))])
}
fn kl_date(value, cosmic = false) {
    if (value==null) null else if (cosmic) decimal(value.year)
    else {
        let fields=["year","month","day","hour","minute","second","millisecond"];
        let record=map([for (key in fields where value[key]!=null and value[key]!="") (key,i64(value[key]))]);
        {*:record,year:dates.human_year(record.year),display_date:value.display_date}
    }
}
fn kl_media(value) => if (value==null or value.url==null or value.url=="") [] else
    [{kind:"embed",src:value.url,caption:value.caption,credit:value.credit,thumbnail:value.thumbnail,alt:value.alt,
        content:value.iframe,link:value.link}]
fn kl(data, options) {
    let cosmic=data.scale=="cosmological";
    let values=[for (index,item in c.items(data.events)) {
        id:if (item.unique_id!=null) c.text(item.unique_id) else "event-" ++ string(index),
        unstable_id:item.unique_id==null,
        from:if (item.end_date!=null) kl_date(item.start_date,cosmic) else null,
        to:if (item.end_date!=null) kl_date(item.end_date,cosmic) else null,
        at:if (item.end_date==null) kl_date(item.start_date,cosmic) else null,
        title:item.text.headline,content:item.text.text,display_date:item.display_date,
        group:if (item.group!=null and item.group!="") c.text(item.group) else null,
        media:kl_media(item.media),metadata:{timelinejs:item},appearance:item.background}];
    let groups=util.unique_vals(values |> ~.group) |: ~!=null;
    {view:"story",events:values,groups:[for (id in groups) {id:id,title:id}],
        scales:if (cosmic) [{kind:"cosmological",unit:"year"}] else [],
        title:data.title.text.headline,description:data.title.text.text,
        story:{title_slide:if (data.title==null) null else {id:c.text(c.option(data.title,"unique_id","__title__")),
            title:data.title.text.headline,content:data.title.text.text,media:kl_media(data.title.media),
            metadata:{timelinejs:data.title},appearance:data.title.background}},
        eras:[for (index,item in c.items(data.eras)) {id:c.text(c.option(item,"unique_id","era-" ++ string(index))),
            from:kl_date(item.start_date,cosmic),to:kl_date(item.end_date,cosmic),title:item.text.headline}],
        *:c.attrs(options)}
}
fn chrono_item(item, index, path) {
    let id=if (item.id!=null) c.text(item.id) else path ++ string(index);
    {id:id,unstable_id:item.id==null,at:item.date,display_date:item.title,title:c.option(item,"cardTitle",item.title),
        subtitle:item.cardSubtitle,content:c.option(item,"cardDetailedText",c.option(item,"timelineContent",item.content)),
        link:item.url,visible:c.option(item,"visible",true),marker:item.iconChild,
        media:if (item.media!=null) [{kind:lower(item.media.type),src:item.media.source.url,mime:item.media.source.type,alt:item.media.name}] else [],
        children:if (item.items!=null) [<timeline.chart spec:{events:[for (i,child in item.items) chrono_item(child,i,id ++ "/")],placement:"alternate"}>] else null,
        metadata:{chrono:item}}
}
fn vis_date(value) {
    if (value is number) {
        let record=dates.from_coordinate(value);
        if (record is error) record else {year:record.year,month:record.month,day:record.day,hour:record.hour,minute:record.minute,
            second:record.second,millisecond:record.millisecond,timezone:0}
    } else value
}
fn vis(data, options) {
    let items=if (data is array) data else c.items(data.items);
    let groups=c.items(data.groups);
    {view:"axis",events:[for (index,item in items) {
        id:c.text(c.option(item,"id","event-" ++ string(index))),
        unstable_id:item.id==null,
        kind:if (item.type=="background") "background" else if (item.end!=null) "range" else "point",
        at:if (item.end==null) vis_date(item.start) else null,
        from:if (item.end!=null) vis_date(item.start) else null,to:if (item.end!=null) vis_date(item.end) else null,
        title:if (item.content is string) item.content else item.title,content:item.content,tooltip:item.title,
        group:if (item.group!=null) c.text(item.group) else null,subgroup:if (item.subgroup!=null) c.text(item.subgroup) else null,
        selectable:c.option(item,"selectable",true),editable:item.editable,class:item.className,style:item.style,metadata:{vis:item}}],
        groups:[for (group in groups,
            let parents=[for (parent in groups where contains(c.items(parent.nestedGroups),group.id)) parent.id])
            {id:c.text(group.id),title:group.content,parent:if (len(parents)>0) c.text(parents[0]) else null,
                collapsed:group.showNested==false,visible:c.option(group,"visible",true),order:group.order,class:group.className,style:group.style}],
        *:c.attrs(options)}
}

// Resource composition validates after combining decoded and authored collections.
pub fn decode(data, format, options = {}) {
    let kind=lower(c.text(format));
    let decoded=if (contains(["csv","tsv","timelinejs_csv","timelinejs_tsv"],kind)) loaded(data,if (ends_with(kind,"tsv")) "tsv" else "csv")
        else if (data is string) loaded(data,"json") else data;
    let converted=if (decoded is error) decoded else if (contains(["timelinejs","timelinejs_json"],kind)) kl(decoded,options)
        else if (contains(["timelinejs_csv","timelinejs_tsv","timelinejs_spreadsheet","sheets"],kind)) {
            let records=spreadsheet(decoded);
            if (records is error) records else kl(records,options)
        } else if (kind=="chrono" or kind=="react_chrono") {events:[for (index,item in c.items(decoded)) chrono_item(item,index,"event-")],*:c.attrs(options)}
        else if (kind=="vis" or kind=="vis_timeline") vis(decoded,options)
        else if (contains(["json","csv","tsv","records"],kind)) if (decoded is array) {data:decoded,*:c.attrs(options)}
            else if (decoded is map) {*:c.attrs(decoded),*:c.attrs(options)} else c.fail("convert.data","expected records or a timeline specification map")
        else c.fail("convert.format","unsupported source format");
    converted
}
pub fn convert(data, format, options = {}) {
    let converted=decode(data,format,options);
    let valid=if (converted is error) converted else schema.validate(converted);
    if (valid is error) valid else converted
}
