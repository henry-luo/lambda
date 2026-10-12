import timeline: lambda.chart.timeline

let french=timeline.model({id:"french",locale:"fr-FR",width:500,search:{mode:"filter",fields:["title"]},
    events:[{id:"a",at:"2026-10-11",title:"Alpha"},{id:"b",at:"2026-10-12",title:"Beta"}]});
let arabic=timeline.model({id:"arabic",locale:"ar-EG",width:500,interaction:{focus_on_load:true},
    events:[{id:"a",at:"2026-10-11",title:"ألف"},{id:"b",at:"2026-10-12",title:"باء"}]});
<html <body style:"font-family:Arial;margin:16px;",apply(french);apply(arabic)>>
