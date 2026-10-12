import timeline: lambda.chart.timeline
import ui: lambda.ui.dtna
let history=timeline.model({id:"cards-history",width:800,cards:{heading_level:2,read_more:true,
    breakpoints:[{max_width:900,orientation:"vertical"}]},orientation:"horizontal",search:{fields:["title"],mode:"filter"},
    events:[{id:"a",title:"Alpha",content:"Long details for the first event."},{id:"b",title:"Beta",content:<p "Rich details">}]},{width:800,height:500})
let skinned=ui.timeline({id:"dtna-history",timeline_options:{toolbar:{buttons:["next"]},events:[{id:"a",title:"First"},{id:"b",title:"Second"}]}})^;
<html <head <style ui.css> <style "body{font-family:Arial;margin:20px}h2{font-size:18px}">>
    <body <div id:"neutral",apply(history)> <div id:"skinned",ui.render(skinned)>>>
