import timeline: lambda.chart.timeline
let spec={id:"search-overview",view:"axis",width:720,height:320,scales:[{kind:"numeric",unit:"year"}],
    window:{from:0,to:50,min:0,max:100},navigator:{visible:true},
    search:{mode:"highlight",fields:["title"],diacritics:"ignore",highlight:true},
    events:[{id:"a",at:0,title:"École"},{id:"b",at:50,title:"Library"},{id:"c",at:100,title:"École archive"}]}
let model=timeline.model(spec);
<html <head <style "body{font-family:Arial;margin:16px}button{margin:4px;padding:4px}.lambda-timeline-navigator-handle{display:inline-block;width:300px}input[type=range]{width:200px}">>
    <body apply(model)>>
