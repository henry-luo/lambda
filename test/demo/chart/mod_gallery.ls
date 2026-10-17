import chart: lambda.chart.chart
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

pub fn panel(id, title, caption, image, wide = false) => <section id:id,class:if (wide) "card wide" else "card",
    <h2 title>; <p class:"caption",caption>; <div class:"chart-frame",image>>;

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
