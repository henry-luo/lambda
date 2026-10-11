import dom

// plain HTML isolates the engine contracts used by responsive dtna components.
view foundation_engine_contracts: <engine_contracts> state activations:0 {
    <main
        <div id:"media-probe",<input id:"retained-input",value:"kept">>
        <div id:"rtl-row",class:"row",dir:"rtl",<div id:"rtl-child",style:"margin-right:100px;">>
        <div id:"reverse-row",class:"row",style:"flex-direction:row-reverse;",<div id:"reverse-child",style:"margin-right:50px;">>
        <div id:"double-reverse",class:"row",dir:"rtl",style:"flex-direction:row-reverse;",<div id:"double-child",style:"margin-left:50px;">>
        <div id:"reverse-column",style:"display:flex;flex-direction:column-reverse;width:400px;height:100px;",
            <div id:"column-child",style:"width:100px;height:20px;margin-bottom:20px;">>
        <fieldset disabled:true,
            <legend <button id:"legend-button",type:"button","Legend exemption">>
            <button id:"disabled-button",type:"button",<span id:"disabled-label","Disabled">>>
        <button id:"enabled-button",type:"button","Enabled">
        <span id:"activations",string(activations)>>
}
on click(evt) {
    let button = dom.closest(evt.target,"button")
    if (button != null) { activations = activations + 1 }
    'pass'
}
<html <head <style "body{margin:0} .row{display:flex;width:400px;height:30px} .row>div{width:100px;height:20px;flex-shrink:0} #media-probe{--probe-width:100px;width:var(--probe-width);height:30px} #retained-input{width:80px} @media(min-width:768px){#media-probe{--probe-width:200px}}">>
    <body apply(<engine_contracts>)>>
