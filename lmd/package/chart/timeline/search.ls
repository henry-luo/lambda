// search spans retain authored code-point offsets (S17.4.1, S17.7.1).
import c: .common
import dates: .date
import parse: lambda.chart.parse
import util: lambda.chart.util
import radiant

fn mark(ch) {
    let category=lambda.io.unicode_category(ord(ch));
    category=="Mn" or category=="Mc" or category=="Me"
}
fn after_marks(value, index) => if (index<len(value) and mark(value[index])) after_marks(value,index+1) else index

fn prepared(value, options) {
    let source=rich_text(value);
    let ignore=c.text(options.diacritics)=="ignore";
    let points=[for (index in 0 to len(source)-1,
        let expanded=if (ignore) normalize(source[index],"nfd") else source[index])
        for (ch in [for (at in 0 to len(expanded)-1) expanded[at]] where not ignore or not mark(ch))
            {text:ch,from:index,to:if (ignore) after_marks(source,index+1) else index+1}];
    {text:join(points |> ~.text,""),points:points}
}

fn merge_span(spans, span) {
    let previous=spans[len(spans)-1];
    if (previous!=null and span.from<=previous.to)
        [*slice(spans,0,len(spans)-1),{from:previous.from,to:max(previous.to,span.to)}]
    else [*spans,span]
}

pub fn spans(value, query, options = {}) {
    if (c.text(query)=="") [] else {
        let hay=prepared(value,options);
        let needle=prepared(query,options).text;
        let found=if (needle=="") [] else find(hay.text,needle,{ignore_case:options.case_sensitive!=true});
        reduce([[],*[for (hit in found) {from:hay.points[hit.index].from,to:hay.points[hit.index+len(hit.value)-1].to}]],merge_span)
    }
}

fn field_matches(item, field, query, options, context) {
    let authored=parse.channel_value({field:c.text(field)},item);
    let value=if (authored is fn) authored(item,context) else authored;
    // Tags are separate labels; adjacent rich children share one rendered text stream.
    let rendered=[for (part in (if (c.text(field)=="tags") c.items(value) else [value])) c.render(part)];
    let failure=util.first_error([value,*rendered]);
    if (failure is error) failure else any([for (part in rendered) len(spans(part,query,options))>0])
}

pub fn matches(item, query, options = {}, context = {}) {
    let fields=c.option(options,"fields",["title","subtitle","content","display_date","tags"]);
    let found=if (query=="") [true] else [for (field in fields) field_matches(item,field,query,options,context)];
    let predicate=if (options.predicate is fn) options.predicate(item,query) else true;
    let failure=util.first_error([*found,predicate,
        if (not (predicate is bool or predicate is error)) c.fail("search.predicate","expected a boolean result")]);
    if (failure is error) failure else any(found) and predicate
}

// Content offsets cross inline element boundaries without changing authored text.
fn rich_text(value) => if (value is element)
        if (contains(["script","style","template"],lower(c.text(name(value))))) "" else rich_text(content(value))
    else if (value is array) join([for (item in value) rich_text(item)],"")
    else if (value is string or value is symbol or value is number or value is bool) c.text(value) else ""

fn marked_text(source, hits) {
    [*[for (index,hit in hits)
        for (piece in [slice(source,if (index==0) 0 else hits[index-1].to,hit.from),
            <mark class:"lambda-timeline-match",slice(source,hit.from,hit.to)>]) piece],
        slice(source,if (len(hits)==0) 0 else hits[len(hits)-1].to,len(source))]
}

fn rich_highlight(value, hits, offset) {
    let length=len(rich_text(value));
    if (length==0 or not any([for (hit in hits) hit.to>offset and hit.from<offset+length])) value
    else if (value is element) {
        let children=content(value);
        let positions=math.cumsum([0,*[for (item in children) len(rich_text(item))]]);
        radiant.present_content(value,[for (index,item in children)
            for (piece in (if (not (item is element or item is array))
                c.items(rich_highlight(item,hits,offset+positions[index]))
                else [rich_highlight(item,hits,offset+positions[index])])) piece])
    }
    else if (value is array) {
        let positions=math.cumsum([0,*[for (item in value) len(rich_text(item))]]);
        [for (index,item in value) rich_highlight(item,hits,offset+positions[index])]
    } else if (value is string or value is symbol or value is number or value is bool) {
        let source=c.text(value);
        let local=[for (hit in hits where hit.to>offset and hit.from<offset+len(source))
            {from:max(0,hit.from-offset),to:min(len(source),hit.to-offset)}];
        if (len(local)==0) value else marked_text(source,local)
    } else value
}

pub fn highlight(value, query, options = {}) {
    if (options.highlight!=true or query=="" or value==null) value else {
        let hits=spans(rich_text(value),query,options);
        if (len(hits)==0) value else rich_highlight(value,hits,0)
    }
}

pub fn filters(spec, snapshot = null) {
    let raw={*:map([for (key in ["groups","tags","from","to"] where c.has(spec.search,key)) (key,spec.search[key])]),*:c.attrs(snapshot.filters)};
    let from=if (raw.from!=null) dates.normalize(raw.from,spec.scale) else null;
    let to=if (raw.to!=null) dates.normalize(raw.to,spec.scale) else null;
    let failure=util.first_error([from,to,
        for (key in ["groups","tags"] where raw[key]!=null) if (not (raw[key] is array)) c.fail("filters." ++ key,"expected an array"),
        if (from!=null and to!=null and from.value>to.value) c.fail("filters","from must not follow to")]);
    if (failure is error) failure else {*:raw,from:from.value,to:to.value}
}

pub fn accepts(item, filters) =>
    (filters.groups==null or contains(filters.groups,item.group)) and
    (filters.tags==null or any([for (tag in c.items(item.tags)) contains(filters.tags,tag)])) and
    (filters.from==null or item.end==null or item.end.value>=filters.from) and
    (filters.to==null or item.start==null or item.start.value<=filters.to)
