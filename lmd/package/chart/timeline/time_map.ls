// hidden intervals share one exact piecewise mapping for projection and picking.
import c: .common
import dates: .date
import util: lambda.chart.util

fn rule(value, scale) {
    let raw=c.attrs(value);
    let from=dates.normalize(raw.from,scale);
    let to=dates.normalize(raw.to,scale);
    let compression=c.option(raw,"compression",0);
    let repeat=if (raw.repeat!=null) c.text(raw.repeat) else null;
    let failure=util.first_error([c.properties(raw,["from","to","repeat","compression"],"scale.hidden_intervals"),from,to,
        if (from.value>=to.value) c.fail("scale.hidden_intervals","from must precede to"),
        if (not util.finite_number(compression) or compression<0 or compression>1) c.fail("scale.hidden_intervals.compression","expected a ratio from 0 to 1"),
        if (repeat!=null and not contains(["day","week","month","year"],repeat)) c.fail("scale.hidden_intervals.repeat","expected day, week, month, or year"),
        if (repeat!=null and scale.kind!="calendar") c.fail("scale.hidden_intervals.repeat","civil recurrence requires a calendar scale")]);
    if (failure is error) failure else {from:from,to:to,repeat:repeat,compression:compression}
}

pub fn validate(scale) {
    let values=scale.hidden_intervals;
    if (values!=null and not (values is array)) c.fail("scale.hidden_intervals","expected an array of interval records")
    else util.first_error([for (value in c.items(values)) rule(value,scale)])
}

fn cycle(value, anchor, unit) {
    if (unit=="year") value.year-anchor.year
    else if (unit=="month") (value.year-anchor.year)*12+value.month-anchor.month
    else dates.floor_div(dates.days(value.year,value.month,value.day)-dates.days(anchor.year,anchor.month,anchor.day),if (unit=="week") 7 else 1)
}
fn occurrences(item, scale, domain, limit) {
    let from=if (item.repeat!=null) cycle(dates.from_coordinate(domain.from,scale),item.to,item.repeat)-1 else 0;
    let to=if (item.repeat!=null) cycle(dates.from_coordinate(domain.to,scale),item.from,item.repeat)+1 else 0;
    if (to-from+1>limit) c.fail("scale.hidden_intervals","recurrence exceeds interval_limit; narrow the visible window or increase the explicit limit")
    else [for (index in from to to,
        let start=if (item.repeat==null) item.from else dates.shift_calendar(item.from,item.repeat,index,scale),
        let end=if (item.repeat==null) item.to else dates.shift_calendar(item.to,item.repeat,index,scale))
        if (start is error) start else if (end is error) end else if (end.value>domain.from and start.value<domain.to)
            {from:max(start.value,domain.from),to:min(end.value,domain.to),compression:item.compression}]
}

fn segments(boundaries, spans, index = 0, offset = 0m) {
    if (index>=len(boundaries)-1) [] else {
        let from=boundaries[index];
        let to=boundaries[index+1];
        let active=[for (span in spans where span.from<=from and span.to>=to) span.compression];
        let compression=if (len(active)==0) 1 else min(active);
        let width=decimal(to-from)*decimal(compression);
        [{from:from,to:to,compression:compression,offset:offset,width:width},
            *segments(boundaries,spans,index+1,offset+width)]
    }
}

pub fn configure(scale, domain) {
    let parsed=[for (value in c.items(scale.hidden_intervals)) rule(value,scale)];
    let limit=c.option(scale,"interval_limit",10000);
    let spans=[for (item in parsed) if (item is error) item else occurrences(item,scale,domain,limit)];
    let failure=util.first_error([*parsed,*spans]);
    if (failure is error) failure else {
        let all=[for (items in spans) for (item in items where item!=null) item];
        let boundaries=sort(unique([domain.from,domain.to,for (span in all) for (value in [span.from,span.to]) value]));
        let parts=segments(boundaries,all);
        let size=sum(parts |> ~.width);
        if (size<=0) c.fail("scale.hidden_intervals","the visible window is entirely hidden")
        else {domain:domain,segments:parts,size:size,hidden:all |: ~.compression<1}
    }
}

pub fn project(plan, value) {
    let segment=if (value<plan.domain.from) plan.segments[0] else if (value>=plan.domain.to) plan.segments[len(plan.segments)-1]
        else (plan.segments |: value>=~.from and value<~.to)[0];
    let distance=segment.offset+decimal(value-segment.from)*decimal(segment.compression);
    {fraction:float(distance)/float(plan.size),hidden:segment.compression<1,compression:segment.compression}
}
pub fn fraction(plan, value) => project(plan,value).fraction

// at a collapsed seam the caller chooses the previous end or the next start.
pub fn invert(plan, fraction, bias = "forward") {
    let distance=decimal(fraction)*plan.size;
    let candidates=plan.segments |: ~.compression>0 and distance>=~.offset and distance<=~.offset+~.width;
    let segment=if (len(candidates)>0) candidates[if (bias=="backward") 0 else len(candidates)-1]
        else if (distance<0) (plan.segments |: ~.compression>0)[0]
        else {let usable=plan.segments |: ~.compression>0; usable[len(usable)-1]};
    dates.offset(segment.from,(distance-segment.offset)/decimal(segment.compression))
}
