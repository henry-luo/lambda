import timeline: lambda.chart.timeline
import dates: lambda.chart.timeline.date
import axis: lambda.chart.timeline.axis
import calendar: lambda.chart.calendar
let spec={id:"axes",view:"axis",width:800,height:400,window:{from:"2024-01-01",to:"2024-02-01"},
    scales:[{kind:"calendar",timezone:"UTC",week_start:1}],events:[],
    axes:[{id:"weeks",position:"both",tick_unit:"week",week_numbers:true,minor:true,grid:true,title:"Weeks"},
        {id:"dates",position:"start",tick_unit:"week",format:"MMM DD",rotation:30}]}
let plan=timeline.layout(spec,{width:800,height:400})
let image=timeline.render_svg(spec,{width:800,height:400})
let plot=(content(image) |: ~.class=="lambda-timeline-plot")[0]
let numeric={id:"collisions",view:"axis",width:180,height:300,scales:[{kind:"numeric",unit:"year"}],window:{from:0,to:100},
    axes:[{tick_count:15,formatter:(stamp,ctx)=>"Very long date " ++ string(stamp.value),collision:"omit"}],events:[]}
let crowded=timeline.layout(numeric,{width:180,height:300})
let free=timeline.layout({*:numeric,axes:[{*:numeric.axes[0],collision:"none"}]},{width:180,height:300})
let sunday=calendar.ticks(dates.normalize("2024-01-01").value,dates.normalize("2024-01-15").value,{interval:"week",week_start:0},0);
[
 ["multiple axes",len(plan.axes)==2],
 ["both sides",len(plan.axes[0].labels)==len(plan.axes[0].ticks)*2],
 ["major and minor",len(plan.axes[0].minor)>len(plan.axes[0].ticks)],
 ["week numbers",plan.axes[0].ticks[0].label=="W1"],
 ["Monday alignment",dates.from_coordinate(plan.axes[0].ticks[0].value).day==1],
 ["Sunday compatibility",dates.from_coordinate(sunday[0]).day==7],
 ["year boundary ISO week",axis.week_number({year:2021,month:1,day:1})==53 and axis.week_number({year:2024,month:12,day:30})==1],
 ["explicit label rotation",all(plan.axes[1].labels |> ~.angle==30)],
 ["measured collision omission",crowded.axes[0].omitted>0 and len(crowded.axes[0].labels)<len(free.axes[0].labels)],
 ["disabled axis",len(timeline.layout({*:numeric,axes:[{position:"none"}]},{width:180,height:300}).ticks)==0],
 ["rendered axes",len(content(plot) |: ~.class=="lambda-timeline-axis")==2],
 ["week start validation",timeline.validate({*:spec,scales:[{week_start:7}]}) is error],
 ["rotation validation",timeline.validate({*:spec,axes:[{rotation:"sideways"}]}) is error],
 ["step validation",timeline.validate({*:spec,axes:[{tick_step:0.5}]}) is error]
]
