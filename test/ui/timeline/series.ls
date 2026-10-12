import timeline: lambda.chart.timeline
let samples=[{at:0,value:0,category:"A"},{at:2,value:20,category:"A"},{at:5,value:60,category:"A"},{at:8,value:40,category:"A"},{at:10,value:100,category:"A"}]
let model=timeline.model({id:"quantitative-history",view:"axis",width:800,height:480,
    coordinate:{kind:"serpentine",levels:3,lane_width:80},
    scales:[{kind:"numeric",unit:"year",domain:[0,10]},{id:"measure",kind:"linear",domain:[0,100]}],
    axes:[{position:"end",tick_count:6},{scale:"measure",title:"Measure",position:"start",tick_count:3}],
    events:[{id:"first",at:0,title:"First milestone"},{id:"last",at:10,title:"Last milestone"}],
    series:[{id:"line",data:samples,value_scale:"measure",mark:{kind:"line",stroke_width:3,color:"#e45756"},
        encoding:{x:{field:"at"},y:{field:"value",dtype:"quantitative"}}},
        {id:"points",data:samples,value_scale:"measure",mark:"point",encoding:{x:{field:"at"},y:{field:"value",dtype:"quantitative"},color:{field:"category",dtype:"nominal"}}}]});
<html <head <style "body{font-family:Arial;margin:16px}button{padding:4px;margin:4px}">> <body apply(model)>>
