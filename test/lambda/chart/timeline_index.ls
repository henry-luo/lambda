import intervals: lambda.chart.timeline.index

fn point(id, value) => {id:id,start:{value:value},end:{value:value}}
let events=[point("late",9),{id:"open-start",start:null,end:{value:2}},point("edge",2),
    {id:"range",start:{value:1},end:{value:8}},{id:"open-end",start:{value:7},end:null},
    {id:"unbounded",start:null,end:null},point("early",0)]
let source={events:events,groups:[{id:"z",title:"Last"},{id:"a",title:"First"}]}
let index=intervals.build(source)
let large=intervals.build({events:[for (position in 0 to 4095) point("p" ++ string(position),position)],groups:[]})
let local=intervals.query(large,{from:2000,to:2002})
let origin=1000000000000000000000000000000m
let exact=intervals.build({events:[point("before",origin-0.01m),point("at",origin),point("after",origin+0.01m)],groups:[]})
let empty=intervals.build({events:[],groups:[]});
[
 ["query restores canonical source order",(intervals.query(index,{from:2,to:7}).events |> ~.id)==["open-start","edge","range","open-end","unbounded"]],
 ["both interval endpoints are inclusive",(intervals.query(index,{from:8,to:9}).events |> ~.id)==["late","range","open-end","unbounded"]],
 ["open start has no lower bound",(intervals.query(index,{from:-100,to:-50}).events |> ~.id)==["open-start","unbounded"]],
 ["open end has no upper bound",(intervals.query(index,{from:100,to:101}).events |> ~.id)==["open-end","unbounded"]],
 ["complete window returns every source row",intervals.query(index,{from:-100,to:100}).events==events],
 ["empty source queries are empty",intervals.query(empty,{from:0,to:1}).events==[]],
 ["ID lookup follows canonical position",intervals.event(index,"early")==events[6]],
 ["missing event ID is absent",intervals.event(index,"missing")==null],
 ["group lookup uses sorted IDs",intervals.group(index,"a").title=="First"],
 ["missing group ID is absent",intervals.group(index,"missing")==null],
 ["narrow query contains exactly three points",(local.events |> ~.start.value)==[2000,2001,2002]],
 ["narrow query examines bounded leaves",local.examined<=32 and local.nodes<64],
 ["outside query prunes at the root",intervals.query(large,{from:5000,to:5001}).examined==0],
 ["exact decimal neighbors remain distinct",(intervals.query(exact,{from:origin,to:origin}).events |> ~.id)==["at"]],
 ["index construction preserves original source",source.events==events and source.groups[0].id=="z"],
 ["replaced index contains only replacement rows",intervals.event(intervals.build({events:[point("replacement",1)],groups:[]}),"early")==null]
]
