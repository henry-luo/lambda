import timeline: lambda.chart.timeline
let model=timeline.model({id:"geometry",view:"axis",width:900,height:600,coordinate:{kind:"serpentine",levels:3,turn_radius:35},
    scales:[{kind:"numeric",unit:"year",hidden_intervals:[{from:40,to:45}]}],window:{from:0,to:100},
    axes:[{id:"major",position:"both",tick_count:5,minor:true,grid:true,title:"Historical years"},{id:"labels",position:"start",tick_count:5,rotation:20}],
    groups:[{id:"science",title:"Discoveries",height:100}],cards:{width:140,height:70},toolbar:false,
    events:[{id:"early",at:20,title:"Approximate date",group:"science",uncertainty:{earliest:18,latest:22,label:"About year 20"}},
        {id:"period",from:30,to:60,title:"A long period",group:"science"},{id:"later",at:70,title:"Later event",group:"science"}],
    eras:[{id:"era",from:0,to:35,title:"Early era",color:"#dbeafe"}],
    annotations:[{id:"reference",at:50,title:"Reference",color:"#dc2626"},{id:"band",from:75,to:85,title:"Reference period"}]});
<html <body style:"font-family:Arial;margin:20px;",apply(model)>>
