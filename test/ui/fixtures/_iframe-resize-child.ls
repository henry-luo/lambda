import dom

edit <body> state loads:0, clicks:0 {
    <body id:"counter-body",style:"margin:0",'data-loads':string(loads),'data-clicks':string(clicks),
        <button id:"counter",type:"button","Keep this document's state">>
}
on load(evt) { loads = loads+1 }
on click(evt) {
    if (dom.closest(evt.target,"#counter") != null) { clicks = clicks+1 }
}
<html apply(<body>,{mode:"edit"})>
