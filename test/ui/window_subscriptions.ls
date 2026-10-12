import dom

view <window_listener> state hits:0,enabled:true {
    <section id:~.id,
        <button id:~.id++"-toggle",type:"button","Toggle subscription">
        <output id:~.id++"-count",string(hits)>
    >
}
on render(evt) { dom.watch_window(evt.currentTarget,"hashchange",enabled) }
on hashchange(evt) { hits=hits+1 }
on click(evt) { enabled=not enabled; 'pass' }

let first=<window_listener id:"first">;
let second=<window_listener id:"second">;
view <late_window_listener> state hits:0 { <output id:"late-count",string(hits)> }
on hashchange(evt) { hits=hits+1 }
let late=<late_window_listener>;
view <window_demo> state show_second:true,body_events:0 {
    <html <body style:"margin:0;font-family:Arial;", *[
        <button id:"remove",type:"button","Remove second listener">,
        <a id:"one",href:"#one","One">, <a id:"two",href:"#two","Two">,
        <a id:"three",href:"#three","Three">, <a id:"four",href:"#four","Four">,
        <output id:"body-count",string(body_events)>,
        apply(first), if (show_second) apply(second) else null, apply(late)
    ]>>
}
on hashchange(evt) {
    body_events=body_events+1;
    dom.watch_window(dom.query_selector(evt.currentTarget,"#late-count"),"hashchange",true)
}
on click(evt) {
    if (dom.get_attribute(evt.target,"id")=="remove") { show_second=false }
    'pass'
}
apply(<window_demo>)
