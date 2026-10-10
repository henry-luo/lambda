import ui: lambda.ui.dtna
ui.page(<main style:"width:400px;",*ui.render([
    ui.row({id:"responsive-row",gutter:[{xs:16,md:24},12]},[
        ui.col({id:"responsive-left",xs:24,md:12,xxxl:6},<div style:"height:20px;","Left">)^,
        ui.col({id:"responsive-right",xs:24,md:12,xxxl:6},<div style:"height:20px;","Right">)^])^,
    ui.row({id:"ordered",style:"margin-top:24px;"},[
        ui.col({id:"offset",span:6,offset:6,order:2},<div style:"height:20px;","Second">)^,
        ui.col({id:"first",span:6,order:1},<div style:"height:20px;","First">)^])^,
    <div dir:"rtl",ui.render(ui.row({style:"margin-top:24px;"},
        ui.col({id:"rtl-offset",span:6,offset:6},<div style:"height:20px;","RTL">)^)^)>,
    ui.row({style:"margin-top:24px;"},ui.col({id:"hidden-column",xs:0,md:8},<div style:"height:20px;","Responsive visibility">)^)^,
    ui.space({id:"split-space",separator:"/",style:"margin-top:24px;"},["One",null,"Two","Three"])^,
    ui.space({id:"wrapped",gap:8,wrap:true,style:"width:120px;margin-top:24px;"},[
        <div style:"width:60px;height:20px;","One">,<div style:"width:60px;height:20px;","Two">])^,
    ui.space_compact({id:"vertical-compact",direction:'vertical',style:"margin-top:24px;"},[
        ui.button({id:"compact-first"},"First")^,ui.button({id:"compact-last"},"Last")^])^,
    ui.divider({id:"labelled",placement:'start',dashed:true,plain:true},"Section")^,
    ui.divider({id:"vertical-divider",direction:'vertical',dashed:true})^
])>)^
