import timeline: lambda.chart.timeline
import dom

let spec={id:"first-history",view:"cards",orientation:"horizontal",placement:"start",interaction:{multiselect:true},
    events:[{id:"a",at:"2010",title:"Alpha"},{id:"b",at:"2020",title:"Beta"},{id:"c",at:"2025",title:"Gamma"}]}
let first=timeline.model(spec)
let second=timeline.model({*:spec,id:"second-history"})

view <timeline_demo> state counter:0 {
    <html <head <style "body{font-family:Arial;margin:20px}button{padding:4px 8px;margin:4px}.lambda-timeline-chart{margin:16px 0}">>
        <body <button id:"parent-rerender",type:"button","Parent " ++ string(counter)>
            <div id:"first",apply(first)>
            <div id:"second",apply(second)>
        >>
}
on click(evt) {
    if (dom.get_attribute(evt.target,"id")=="parent-rerender") { counter=counter+1 }
    'pass'
}
apply(<timeline_demo>)
