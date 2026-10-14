import ui: lambda.ui.clay
import ant: lambda.ui.dtna
import bold: lambda.ui.bold

let document = ui.page([
    <clay.button id:"base",variant:'primary',"Lavender">,
    <clay.config_provider tokens:{primary:"#287b63",radius:10},
        <clay.button id:"mint",variant:'primary',"Mint">
        <clay.config_provider tokens:{radius:24},<clay.button id:"inherited",variant:'primary',"Inherited mint">>
        <clay.config_provider tokens:{primary:"#bd4561"},<clay.button id:"rose",variant:'primary',"Rose">>>,
    <clay.config_provider disabled:true,
        <clay.input id:"locked",default_value:"Locked",label:"Disabled field">
        <clay.button id:"locked-button","Disabled button">>,
    <clay.button id:"after",variant:'primary',"Lavender again">,
    <clay.card id:"card",title:"Soft surface",<clay.input id:"field",label:"Inset field">>,
    <clay.layout id:"layout",<clay.layout_sider "Side"> <clay.layout_content "Content">>
])^;
<html <head <style ui.stylesheet() ++ bold.stylesheet()>>
    <body *[content(content(document)[1])[0],
        content(content(ant.page(<dtna.button id:"ant",variant:'primary',"Ant">)^)[1])[0],
        content(content(bold.page(<bold.button id:"bold","Bold">)^)[1])[0]]>>
