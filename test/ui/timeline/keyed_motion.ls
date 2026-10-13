import timeline: lambda.chart.timeline
import dom

let axis={id:"motion-axis",view:"axis",width:600,height:320,animate:{duration:200},
    scales:[{kind:"numeric",unit:"year"}],window:{from:0,to:100},
    events:[{id:"a",at:20,title:"Alpha"},{id:"b",at:60,title:"Beta"}]};
let cards={id:"motion-cards",width:600,animate:{duration:200},events:[{id:"a",title:"Alpha"},{id:"b",title:"Beta"}]};
let axis_model=timeline.model(axis);
let cards_model=timeline.model(cards);
let reduced={*:axis,id:"motion-reduced",height:180};
let reduced_model=timeline.model(reduced,null,{reduced_motion:true});
view <motion_demo> {
    <html <body style:"font-family:Arial;margin:12px;",
        <button id:"replace-axis",type:"button","Replace axis">
        <button id:"retarget-axis",type:"button","Interrupt axis">
        <button id:"replace-cards",type:"button","Replace cards">
        <button id:"replace-reduced",type:"button","Replace without motion">
        apply(axis_model);apply(cards_model);apply(reduced_model)>>
}
on click(evt) {
    let id=dom.get_attribute(evt.target,"id");
    let root=dom.closest(evt.target,"body");
    if (contains(["replace-axis","retarget-axis","replace-cards","replace-reduced"],id)) {
        let spec=if (id=="replace-cards") {*:cards,events:[cards.events[1],{id:"c",title:"Gamma"}]}
            else {*:(if (id=="replace-reduced") reduced else axis),value:{window:{from:0,to:if (id=="replace-axis") 200 else 300}},
                events:[axis.events[1],{id:"c",at:if (id=="replace-axis") 140 else 200,title:"Gamma"}]};
        let host=dom.query_selector(root,if (id=="replace-cards") "#motion-cards" else if (id=="replace-reduced") "#motion-reduced" else "#motion-axis");
        dom.dispatch(host,{type:"timeline_action",detail:{type:"source_update",spec:spec}})
    }
    'pass'
}
apply(<motion_demo>)
