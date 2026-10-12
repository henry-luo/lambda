import timeline: lambda.chart.timeline
import dates: lambda.chart.timeline.date
import mapping: lambda.chart.timeline.time_map

let numeric={kind:"numeric",unit:"year",hidden_intervals:[{from:20,to:40}]}
let plan=mapping.configure(numeric,{from:0,to:100})
let compressed=mapping.configure({*:numeric,hidden_intervals:[{from:20,to:40,compression:0.5}]},{from:0,to:100})
let overlap=mapping.configure({*:numeric,hidden_intervals:[{from:20,to:50},{from:40,to:60}]},{from:0,to:100})
let large=100000000000000000000000m
let deep=mapping.configure({*:numeric,hidden_intervals:[{from:large+2,to:large+4}]},{from:large,to:large+10})
let weekly={kind:"calendar",timezone:"UTC",hidden_intervals:[{from:"2024-01-06",to:"2024-01-08",repeat:"week"}]}
let week=mapping.configure(weekly,{from:dates.normalize("2024-01-01").value,to:dates.normalize("2024-01-15").value})
let daily=mapping.configure({*:weekly,hidden_intervals:[{from:"2024-01-01T18:00Z",to:"2024-01-02T09:00Z",repeat:"day"}]},
    {from:dates.normalize("2024-01-03").value,to:dates.normalize("2024-01-05").value})
let monthly=mapping.configure({*:weekly,hidden_intervals:[{from:"2024-01-30",to:"2024-02-02",repeat:"month"}]},
    {from:dates.normalize("2024-02-01").value,to:dates.normalize("2024-03-05").value})
let yearly=mapping.configure({*:weekly,hidden_intervals:[{from:"2024-02-29",to:"2024-03-02",repeat:"year"}]},
    {from:dates.normalize("2025-02-01").value,to:dates.normalize("2025-04-01").value})
let zone={kind:"calendar",timezone:"America/New_York"}
let spring=mapping.configure({*:zone,hidden_intervals:[{from:"2024-03-02",to:"2024-03-04",repeat:"week"}]},
    {from:dates.normalize("2024-03-08",zone).value,to:dates.normalize("2024-03-12",zone).value})
let spec={id:"compressed",view:"axis",width:800,height:400,scales:[numeric],window:{from:0,to:100},
    events:[{id:"inside",at:30,title:"Hidden date"},{id:"after",at:60,title:"Visible date"}]}
let geometry=timeline.layout(spec,{width:800,height:400})
let svg=timeline.render_svg(spec,{width:800,height:400});
[
 ["excluded span",plan.size==80 and mapping.fraction(plan,60)==0.5],
 ["hidden detection",mapping.project(plan,30).hidden and not mapping.project(plan,40).hidden],
 ["reversible visible mapping",abs(mapping.invert(plan,mapping.fraction(plan,70))-70)<0.000001],
 ["collapsed seam forward",mapping.invert(plan,0.25)==40],
 ["collapsed seam backward",mapping.invert(plan,0.25,"backward")==20],
 ["compressed interval",compressed.size==90 and abs(mapping.invert(compressed,mapping.fraction(compressed,30))-30)<0.000001],
 ["overlap union",overlap.size==60],
 ["exact deep projection",mapping.fraction(deep,large+6)==0.5 and mapping.invert(deep,0.5)==large+6],
 ["weekly recurrence",len(week.hidden)==2 and week.size==10*86400000],
 ["daily recurrence clips",daily.size==18*3600000],
 ["monthly civil recurrence",any([for (span in monthly.hidden) span.from==dates.normalize("2024-02-29").value])],
 ["yearly leap clamp",yearly.hidden[0].from==dates.normalize("2025-02-28").value],
 ["DST civil recurrence",spring.hidden[0].to-spring.hidden[0].from==47*3600000],
 ["timezone inverse",dates.from_coordinate(dates.normalize("2024-03-10T07:00Z").value,zone).hour==3],
 ["time map used by layout",geometry.events[0].hidden and abs(geometry.events[1].fraction-0.5)<0.000001],
 ["hidden event retained",len(geometry.events)==2],
 ["hidden interval disclosed",len(content(content(svg)[4]) |: name(~)=='g' and ~.class=="lambda-timeline-hidden-interval")>0],
 ["all hidden diagnosed",mapping.configure({*:numeric,hidden_intervals:[{from:0,to:100}]},{from:0,to:100}) is error],
 ["recurrence limit diagnosed",mapping.configure({*:weekly,interval_limit:2},{from:dates.normalize("2024-01-01").value,to:dates.normalize("2024-02-01").value}) is error],
 ["invalid compression",timeline.validate({*:spec,scales:[{*:numeric,hidden_intervals:[{from:1,to:2,compression:2}]}]}) is error],
 ["invalid recurrence",timeline.validate({*:spec,scales:[{*:numeric,hidden_intervals:[{from:1,to:2,repeat:"week"}]}]}) is error]
]
