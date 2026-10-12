// historical uncertainty and reference marks retain exact chronology coordinates.
import c: .common
import dates: .date
import util: lambda.chart.util

fn interval(value, anchor, scale, path) {
    let raw=c.attrs(value);
    let earliest=dates.normalize(raw.earliest,scale);
    let latest=dates.normalize(raw.latest,scale);
    let failure=util.first_error([c.properties(raw,["earliest","latest","label"],path),earliest,latest,
        if (anchor==null) c.fail(path,"uncertainty requires a committed date"),
        if (earliest.value>latest.value or earliest.value>anchor.value or latest.value<anchor.value)
            c.fail(path,"uncertainty bounds must enclose the committed date")]);
    if (failure is error) failure else {start:earliest,end:latest,label:raw.label}
}

pub fn uncertainty(value, start, end, kind, scale) {
    if (value==null) null else if (not (value is map)) c.fail("event.uncertainty","expected uncertainty bounds")
    else if (kind=="point") {
        let bounds=interval(value,start,scale,"event.uncertainty");
        if (bounds is error) bounds else {point:bounds}
    } else {
        let first=if (value.start!=null) interval(value.start,start,scale,"event.uncertainty.start") else null;
        let final=if (value.end!=null) interval(value.end,end,scale,"event.uncertainty.end") else null;
        let failure=util.first_error([c.properties(value,["start","end","label"],"event.uncertainty"),first,final,
            if (first==null and final==null) c.fail("event.uncertainty","duration uncertainty requires start or end bounds")]);
        if (failure is error) failure else {start:first,end:final,label:value.label}
    }
}

pub fn normalize(raw, scale, index) {
    let key=if (raw.id!=null) c.id(raw.id) else "annotation-" ++ string(index);
    let kind=c.text(c.option(raw,"kind",if (raw.from!=null or raw.to!=null) "band" else "line"));
    let start=if (raw.at!=null) dates.normalize(raw.at,scale) else if (raw.from!=null) dates.normalize(raw.from,scale) else null;
    let end=if (raw.to!=null) dates.normalize(raw.to,scale) else start;
    let failure=util.first_error([raw,key,start,end,
        if (not contains(["line","rule","point","band","text","current_time","custom_time"],kind)) c.fail("annotation.kind","unknown reference mark"),
        if (raw.at!=null and (raw.from!=null or raw.to!=null)) c.fail("annotation.at","cannot combine at with from/to"),
        if (kind!="current_time" and start==null) c.fail("annotation.date","reference marks require an explicit date"),
        if (kind=="band" and (start==null or end==null or raw.to==null)) c.fail("annotation.date","bands require two endpoints"),
        if (start!=null and end!=null and start.value>end.value) c.fail("annotation.to","end precedes start")]);
    if (failure is error) failure else {*:raw,id:key,kind:kind,start:start,end:end}
}

pub fn resolve(values, snapshot, scale) => [for (value in values,
    let at=if (value.kind=="current_time" and value.start==null and snapshot.current_time!=null) dates.normalize(snapshot.current_time,scale) else null)
    if (value.kind!="current_time") value else if (value.start!=null) value else if (at is error) at
    else if (at!=null) {*:value,start:at,end:at}]
