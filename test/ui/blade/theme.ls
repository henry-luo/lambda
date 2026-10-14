import ui: lambda.ui.blade
import clay: lambda.ui.clay
import ant: lambda.ui.dtna

let document = ui.page([
    <blade.button id:"base",variant:'primary',"Gold">,
    <blade.config_provider tokens:{primary:"#527a9c",gold:"#78b4d4",slot_size:96},
        <blade.button id:"blue",variant:'primary',"Blue">
        <blade.config_provider tokens:{radius:12},<blade.button id:"inherited",variant:'primary',"Inherited blue">>
        <blade.item_slot id:"large-slot",icon:'sword'>
        <blade.meter id:"blue-meter",resource:'mana',value:40>>,
    <blade.button id:"after",variant:'primary',"Gold again">,
    <blade.panel id:"parchment",material:'parchment',title:"Parchment",<blade.text id:"ink","A readable inscription">>,
    <blade.rpg_icon id:"large-art",name:'sword',size:256,label:"Large sword">,
    <blade.config_provider disabled:true,<blade.ability id:"locked",icon:'fire'>>
])^;
<html <head <style ui.stylesheet() ++ clay.stylesheet()>>
    <body *[content(content(document)[1])[0],
        content(content(clay.page(<clay.button id:"clay",variant:'primary',"Clay">)^)[1])[0],
        content(content(ant.page(<dtna.button id:"ant",variant:'primary',"Ant">)^)[1])[0]]>>
