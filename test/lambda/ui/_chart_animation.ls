import chart: lambda.chart.chart
import dom
let encoding={x:{field:"x",dtype:"quantitative",scale:{domain:[0,10]},axis_enabled:false},y:{field:"y",dtype:"quantitative",scale:{domain:[0,10]},axis_enabled:false},key:{field:"id"}}
let first={data:[{id:"a",x:2,y:2}],mark:{kind:"point",size:200},encoding:encoding}
let second={*:first,data:[{id:"a",x:8,y:8}]}
let spec={*:first,width:220,height:170,timeline:{autoplay:true,keyframes:[{at:0,spec:first},{at:1000,spec:second}]}}
let player=chart.model(spec)
view <demo> state phase:"none", notices:0 {
    <html <body <div id:"demo",
        <button id:"pause","Pause">; <button id:"resume","Resume">; <button id:"seek","Seek">;
        <button id:"reverse","Reverse">; <button id:"cancel","Cancel">; <button id:"play","Play">;
        <span id:"phase",phase>; <span id:"notices",string(notices)>;
        apply(~.player)
    >>>
}
on click(evt) {
    let command=dom.get_attribute(evt.target,"id")
    if (not contains(["pause","resume","seek","reverse","cancel","play"],command)) { return 'pass' }
    let host=dom.query_selector(dom.closest(evt.target,"#demo"),".lambda-chart")
    dom.dispatch(host,{type:"chart_action",bubbles:true,detail:{command:command,position:500}})
    return 'handled'
}
on chart_animation(evt) {
    phase=evt.phase
    notices=notices+1
    return 'handled'
}
apply(<demo player:player>)
