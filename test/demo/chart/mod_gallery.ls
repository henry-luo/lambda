import chart: lambda.chart.chart
import sizing: lambda.chart.sizing
import svg: lambda.chart.svg
import events: lambda.chart.events
import dom

pub fn page(title, caption, body) => <html lang:"en",
    <head <meta charset:"utf-8">; <meta name:"viewport",content:"width=device-width,initial-scale=1">;
        <title title ++ " · Lambda charts">; <link rel:"stylesheet",href:"chart.css">>
    <body <main
        <nav class:"breadcrumbs",'aria-label':"Breadcrumb",<a href:"index.html","Charts">; <span " / ">; <span 'aria-current':"page",title>>
        <header <p class:"eyebrow","LAMBDA / CHART COLLECTION">; <h1 title>; <p class:"intro",caption>>
        body;
        <footer <a href:"index.html","← Back to charts">>
    >>>;

pub fn panel(id, title, caption, image, wide = false, attrs = {}) => <section *:attrs,id:id,class:if (wide) "card wide" else "card",
    <h2 title>; <p class:"caption",caption>; <div class:"chart-frame",image>>;

// Reserve authored chart dimensions before generating its SVG, keeping the scroll range stable.
view <deferred_panel> state ready:false {
    let image = if (ready) apply(~.model) else
        <div class:"chart-placeholder",role:"status",'aria-label':"Loading chart",
            svg.svg_root(~.size.width,~.size.height,[],{'aria-hidden':"true"});
            events.controls([],null)>;
    panel(~.id,~.title,"",image,false,{'data-chart-pending':string(not ready),'aria-busy':string(not ready)})
}
on chart_materialize(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    ready = true
    'handled'
}
pub fn deferred(id, title, spec) {
    let size = sizing.resolve_view(spec);
    if (size is error) size else <deferred_panel id:id,title:title,model:chart.model(spec),size:{width:size.width,height:size.height}>
}

view <progressive_gallery> state started:false {
    <div class:"gallery dashboard-gallery",'data-gallery-started':started,
        for (panel in ~.panels) apply(panel)>
}
on render(evt) {
    if (evt.event_phase != 2 or started) { return 'pass' }
    started = true
    dom.request_frame(evt.target,"gallery_chunk")
    'handled'
}
on gallery_chunk(evt) {
    let pending = dom.query_selector_all(evt.target,"[data-chart-pending='true']")
    if (len(pending) == 0) { return 'handled' }
    let viewport = dom.viewport_size(evt.target)
    let visible = [for (node in pending,let box = dom.bounding_box(node)
        where box.bottom > 0 and box.top < viewport.height) node]
    let next = if (len(visible) > 0) visible[0] else pending[0]
    dom.dispatch(next,{type:"chart_materialize",bubbles:false})
    // A new request belongs to the next frame, so input and paint run between charts.
    if (len(pending) > 1) { dom.request_frame(evt.target,"gallery_chunk") }
    'handled'
}
pub fn progressive(panels) => apply(<progressive_gallery panels:panels>)

// Each apply owns its player and controls, including bubbled notifications (S12.1.3).
view <animated_card> state phase:"Ready" {
    <section id:~.id,class:"card animation-card",
        <h2 ~.title>; <p class:"caption",~.caption>;
        <div class:"chart-frame",apply(~.player)>;
        <div class:"playback-controls",
            for (control in [{command:"play",label:"Replay"},{command:"pause",label:"Pause"},
                {command:"resume",label:"Resume"},{command:"seek",label:"Halfway"},{command:"reverse",label:"Reverse"}])
                <button type:"button",'data-play-command':control.command,control.label>;
            <output class:"playback-phase",'aria-live':"polite",phase>
        >
    >
}
on click(evt) {
    let command = dom.get_attribute(evt.target,"data-play-command");
    if (command == null) { return 'pass' }
    let host = dom.query_selector(dom.closest(evt.target,".animation-card"),".lambda-chart");
    dom.dispatch(host,{type:"chart_action",bubbles:true,detail:{command:command,position:500}})
    'handled'
}
on chart_animation(evt) { phase = evt.phase; 'handled' }

pub fn animated(id, title, caption, spec) => apply(<animated_card id:id,title:title,caption:caption,player:chart.model(spec)>)
