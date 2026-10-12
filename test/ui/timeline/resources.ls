import timeline: lambda.chart.timeline
import dom
let history=timeline.model({id:"loaded-history",source:{url:"history.json",format:"json",revision:"r1"}})
view <resource_demo> state request:null,notices:0 {
    <html <head <style "body{font-family:Arial;margin:20px}">>
        <body <main <button id:"deliver",type:"button","Deliver result">
            <output id:"requested",if (request!=null) "requested" else "none">
            <output id:"received",string(notices)>
            apply(history)
        >>>
}
on timeline_request(value) { request=value }
on timeline_resource(value) { notices=notices+1 }
on click(evt) {
    if (dom.get_attribute(evt.target,"id")=="deliver" and request!=null) {
        let root=dom.closest(evt.target,"main");
        let host=dom.query_selector(root,"[data-timeline-host]");
        dom.dispatch(host,{type:"timeline_action",detail:{type:"resource_result",id:request.id,token:request.token,revision:request.revision,
            result:[{id:"loaded",at:"2020",title:"Loaded history"}]}})
    }
    'pass'
}
apply(<resource_demo>)
