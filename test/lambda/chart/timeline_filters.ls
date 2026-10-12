import t: lambda.chart.timeline
import controls: lambda.chart.timeline.filter_controls

let raw={id:"filters",search:{controls:true,groups:["g"],tags:['archive'],from:"2000"},
    groups:[{id:"g",title:"History"},{id:"h",title:"Other"}],
    events:[{id:"a",at:"1999",group:"g",tags:['archive']},{id:"b",at:"2001",group:"h",tags:["recent"]}]};
let spec=t.validate(raw);
let initial=t.initial_state(raw);
let cleared=t.update(initial,{type:"filter",filters:{groups:null,tags:null,from:null,to:null}},{spec:raw}).state;
let limited=t.update(cleared,{type:"filter",filters:{groups:["h"],tags:null,from:"2000",to:"2002"}},{spec:raw});
let html=format(t.render(raw,null,cleared),'html');
let numeric=t.validate({search:{controls:['from','to']},scales:[{kind:"numeric",unit:"year"}]});
[
 ["group filter defaults are reflected",controls.control_value(spec,initial,"groups","g") and not controls.control_value(spec,initial,"groups","h")],
 ["symbol tag values retain identity",controls.control_value(spec,initial,"tags",'archive') and not controls.control_value(spec,initial,"tags","archive")],
 ["calendar input retains precision",controls.control_value(spec,initial,"from")=="2000"],
 ["explicit null clears configured constraints",controls.control_value(spec,cleared,"from")=="" and controls.control_value(spec,cleared,"groups","h")],
 ["date and group filters share activation",limited.state.active_id=="b"],
 ["filter notifications use the ordinary channel",limited.notifications[0].name=="timeline_search"],
 ["native group fieldset is labelled",contains(html,"<legend>Filter groups</legend>")],
 ["native date input has accessible label",contains(html,"aria-label=\"From\"")],
 ["numeric input retains exact decimal",controls.control_value(numeric,{filters:{from:100000000000000000001.25m}},"from")=="100000000000000000001.25"],
 ["record date input retains month precision",controls.control_value(spec,{filters:{from:{year:2026,month:10}}},"from")=="2026-10"],
 ["record timestamp keeps offset and milliseconds",controls.control_value(spec,{filters:{from:{year:2026,month:10,day:11,hour:13,minute:14,second:15,millisecond:25,timezone:480}}},"from")=="2026-10-11T13:14:15.025+08:00"],
 ["control labels follow locale",contains(format(t.render({locale:"fr",search:{controls:["from"]}}),'html'),"aria-label=\"Depuis\"")],
 ["controls disabled",not contains(format(t.render({search:{controls:false}}),'html'),"lambda-timeline-filters")],
 ["duplicate control fields diagnosed",t.validate({search:{controls:["from",'from']}}) is error],
 ["unknown control field diagnosed",t.validate({search:{controls:["other"]}}) is error],
 ["invalid control shape diagnosed",t.validate({search:{controls:{from:true}}}) is error],
 ["reversed filter bounds diagnosed",t.update(cleared,{type:"filter",filters:{from:"2002",to:"2000"}},{spec:raw}) is error],
 ["invalid filter date diagnosed",t.update(cleared,{type:"filter",filters:{from:"bad"}},{spec:raw}) is error]
]
