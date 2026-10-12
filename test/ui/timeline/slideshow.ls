import timeline: lambda.chart.timeline
import dom
let spec={id:"slideshow",view:"cards",orientation:"horizontal",placement:"start",slideshow:{duration:100},
    toolbar:{buttons:["play","pause","resume","stop"]},events:[{id:"a",title:"First"},{id:"b",title:"Second"},{id:"c",title:"Third"}]}
let history=timeline.model(spec)
view <slideshow_demo> state shown:true {
    <html <head <style "body{font-family:Arial;margin:20px}button{padding:4px 8px;margin:4px}">>
        <body <button id:"remove",type:"button","Remove instance">
            if (shown) <div id:"show",apply(history)> else <p id:"removed","Removed">
        >>
}
on click(evt) {
    if (dom.get_attribute(evt.target,"id")=="remove") { shown=false }
    'pass'
}
apply(<slideshow_demo>)
