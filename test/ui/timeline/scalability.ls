import timeline: lambda.chart.timeline
import dom

// two collision rows on each side fit inside the authored chart height.
let history=timeline.model({id:"large-history",view:"axis",width:800,height:800,
    scales:[{kind:"numeric",unit:"year"}],window:{from:5000,to:5008},
    cards:{width:180},toolbar:false,navigator:{visible:false},interaction:{pan:true,zoom:true},
    events:[for (index in 0 to 9999) {id:"event-" ++ string(index),at:index,
        title:"Milestone " ++ string(index),content:"Synthetic history event " ++ string(index)}]});

view <scalability_demo> {
    <html <head <style "body{font-family:Arial;margin:20px;background:#f7f9fc;color:#172033}h1{font-size:24px}button{padding:8px 14px;margin:0 8px 12px 0}">>
        <body <h1 "10,000-event chronology">
            <p "Jump between windows of nine events, drag to pan, or use the mouse wheel to zoom. The full source stays loaded while the visible window is rendered.">
            <button id:"window-start",type:"button","First events">
            <button id:"window-middle",type:"button","Middle events">
            <button id:"window-end",type:"button","Last events">
            apply(history)>>
}
on click(evt) {
    let id=dom.get_attribute(evt.target,"id");
    if (contains(["window-start","window-middle","window-end"],id)) {
        let from=if (id=="window-start") 0 else if (id=="window-middle") 5000 else 9991;
        let host=dom.query_selector(dom.closest(evt.target,"body"),"#large-history");
        dom.dispatch(host,{type:"timeline_action",detail:{type:"set_window",window:{from:from,to:from+8}}})
    }
    'pass'
}
apply(<scalability_demo>)
