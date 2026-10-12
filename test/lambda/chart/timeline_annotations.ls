import timeline: lambda.chart.timeline
let spec={id:"references",view:"axis",width:800,height:400,scales:[{kind:"numeric",unit:"year"}],window:{from:0,to:100},
    events:[{id:"point",at:20,title:"Approximate",uncertainty:{earliest:15,latest:25,label:"About 20"}},
        {id:"range",from:40,to:60,uncertainty:{start:{earliest:35,latest:45},end:{earliest:55,latest:65}}}],
    annotations:[{id:"reference",at:30,title:"Reference"},{id:"period",from:70,to:90,title:"Period"},{id:"now",kind:"current_time",title:"Current"}]}
let normalized=timeline.validate(spec)
let initial=timeline.initial_state(spec)
let changed=timeline.update(initial,{type:"set_current_time",at:50},{spec:spec,revision:7})
let plan=timeline.layout(spec,{width:800,height:400},changed.state)
let image=timeline.render_svg(spec,{width:800,height:400},changed.state)
let plot=(content(image) |: ~.class=="lambda-timeline-plot")[0]
let event=(content(plot) |: ~.'data-timeline-id'=="point")[0];
[
 ["point bounds normalized",normalized.events[0].uncertainty.point.start.value==15 and normalized.events[0].uncertainty.point.end.value==25],
 ["range endpoint uncertainty",normalized.events[1].uncertainty.start.start.value==35 and normalized.events[1].uncertainty.end.end.value==65],
 ["uncertainty enclosure",timeline.validate({*:spec,events:[{id:"bad",at:20,uncertainty:{earliest:21,latest:25}}]}) is error],
 ["uncertainty reversed",timeline.validate({*:spec,events:[{id:"bad",at:20,uncertainty:{earliest:25,latest:15}}]}) is error],
 ["range shape validation",timeline.validate({*:spec,events:[{id:"bad",from:40,to:60,uncertainty:{earliest:35,latest:65}}]}) is error],
 ["annotation inference",normalized.annotations[0].kind=="line" and normalized.annotations[1].kind=="band"],
 ["explicit current time",(plan.annotations |: ~.id=="now")[0].start.value==50],
 ["no ambient clock",len(timeline.layout(spec,{width:800,height:400}).annotations)==2],
 ["cursor notification",changed.notifications[0].name=="timeline_cursor" and changed.notifications[0].detail.source_revision==7],
 ["rendered reference marks",len(content(plot) |: ~.class=="lambda-timeline-annotation")==3],
 ["rendered uncertainty",len(content(event) |: ~.class=="lambda-timeline-uncertainty")==1],
 ["annotation requires date",timeline.validate({*:spec,annotations:[{id:"bad",kind:"line"}]}) is error],
 ["duplicate reference ID",timeline.validate({*:spec,annotations:[{id:"same",at:10},{id:'same',at:20}]}) is error],
 ["reference group validated",timeline.validate({*:spec,annotations:[{id:"bad",at:10,group:"unknown"}]}) is error]
]
