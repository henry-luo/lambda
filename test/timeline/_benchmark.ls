// Release-only workload entry; the Python runner builds and records the executable.
import timeline: lambda.chart.timeline
import prepared: lambda.chart.timeline.prepared
import geometry: lambda.chart.timeline.layout
import reducer: lambda.chart.timeline.state

fn dataset(count, scenario) {
    let calendar=scenario=="large_years";
    let grouped=scenario=="groups";
    let groups=if (grouped) min(1000,max(1,count div 10)) else 0;
    let middle=count div 2;
    {id:"benchmark",view:"axis",width:1000,height:600,
        scales:[{kind:if (calendar) "calendar" else "numeric",unit:if (calendar) null else "year"}],
        window:if (scenario=="identical") {from:-1,to:1} else if (calendar)
            {from:{year:1000000+middle},to:{year:1000000+min(count,middle+32)}}
            else {from:middle,to:min(count,middle+32)},
        cluster:{enabled:scenario=="identical"},
        groups:[for (index in 0 to groups-1) {id:"g" ++ string(index),title:"Group " ++ string(index)}],
        events:[for (index in 0 to count-1) {id:"event-" ++ string(index),
            at:if (calendar) {year:1000000+index} else if (scenario=="identical") 0 else index,
            title:"Event " ++ string(index),group:if (grouped) "g" ++ string(index%groups) else null,
            content:if (scenario=="rich") <div <p *[for (part in 0 to 11)
                <span "History " <em string(index)> " paragraph " string(part)>]>> else null}]}
}

pub pn run(count: int, scenario: string) map | error {
    let start=clock();
    let raw=dataset(count,scenario);
    let generated=clock();
    let cache=prepared.source(raw);
    if (cache.normalized is error) { return cache.normalized }
    let normalized=clock();
    let current=reducer.initial(cache.normalized);
    if (current is error) { return current }
    let initialized=clock();
    let first=geometry.normalized(cache.normalized,{width:1000,height:600},current);
    if (first is error) { return first }
    let laid_out=clock();
    var samples=[];
    var work=[];
    var checksum=len(first.events);
    for (step in 1 to 8) {
        let before=clock();
        let next=prepared.source(raw,current,cache);
        // keep dense identical dates in view throughout the repeated updates.
        let shift=if (scenario=="identical") decimal(step)/10m else step;
        let window={from:first.window.from+shift,to:first.window.to+shift};
        let frame=geometry.normalized(next.normalized,{width:1000,height:600},{*:current,window:window});
        if (frame is error) { return frame }
        checksum=checksum+len(frame.events);
        samples=[*samples,(clock()-before)*1000.0]
        work=[*work,frame.work]
    }
    let before_cache=clock();
    for (step in 1 to 100) {
        let next=prepared.source(raw,{*:current,selected_ids:["event-" ++ string(step)]},cache);
        checksum=checksum+len(next.normalized.events)
    }
    let cache_ms=(clock()-before_cache)*1000.0;
    return {count:count,scenario:scenario,normalized_count:len(cache.normalized.events),
        visible:len(first.visible_events),cards:len(first.events),work:first.work,checksum:checksum,
        generate_ms:(generated-start)*1000.0,normalize_ms:(normalized-generated)*1000.0,
        initialize_ms:(initialized-normalized)*1000.0,layout_ms:(laid_out-initialized)*1000.0,
        viewport_updates_ms:samples,viewport_work:work,cached_source_100_ms:cache_ms}
}
