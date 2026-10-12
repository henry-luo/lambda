import timeline: lambda.chart.timeline
import dom
let spec={id:"bookmarks",view:"story",spacing:"sequence",width:600,height:220,
    story:{bookmarks:true,title_slide:{id:"intro",title:"Introduction"}},toolbar:{buttons:["first","next","last"]},
    events:[{id:"a",title:"First"},{id:"b",title:"Second"}]}
let history=timeline.model(spec)
view <bookmark_demo> state fragment:"",notices:0 {
    <html <body style:"margin:0;font-family:Arial;",<main
        <button id:"read",type:"button","Read location">
        <button id:"restore",type:"button","Restore introduction">
        <button id:"back",type:"button","Back">
        <button id:"forward",type:"button","Forward">
        <a id:"external",href:"#timeline=bookmarks/a","External bookmark">
        <output id:"fragment",fragment> <output id:"notices",string(notices)>
        apply(history)
    >>>
}
on timeline_bookmark(value) { notices=notices+1 }
on click(evt) {
    let key=dom.get_attribute(evt.target,"id");
    if (key=="read") { fragment=dom.owner_document(evt.target).hash }
    else if (key=="back" or key=="forward") { dom.history_traverse(evt.target,if (key=="back") -1 else 1) }
    else if (key=="restore") {
        let host=dom.query_selector(dom.closest(evt.target,"main"),"[data-timeline-host]");
        dom.dispatch(host,{type:"timeline_action",detail:{type:"bookmark",fragment:"#timeline=bookmarks/intro"}})
    }
    'pass'
}
apply(<bookmark_demo>)
