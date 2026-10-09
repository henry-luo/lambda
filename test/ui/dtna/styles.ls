import ui: lambda.ui.dtna
ui.page(ui.flex({direction:'vertical',align:"flex-start"},[
    ui.button({id:"blue",variant:'primary'},"Blue")^,
    ui.config_provider({tokens:{primary:"#722ed1"}},ui.space({},[
        ui.button({id:"purple",variant:'primary'},"Purple")^,
        ui.config_provider({tokens:{radius:0}},ui.button({id:"square",variant:'primary'},"Square")^)^
    ])^)^,
    ui.button({id:"blue-again",variant:'primary',size:'large'},"Blue again")^,
    ui.alert({id:"notice",closable:true,message:"Dismiss me"})^,
    ui.result({id:"outcome",status:'success',title:"Ready"})^,
    ui.config_provider({direction:'rtl'},
        ui.config_provider({tokens:{radius:0}},ui.button({id:"rtl-child"},"Inherited direction")^)^)^,
    ui.icon({id:"sized-icon",name:"check",width:24,height:18})^,
    <div style:"width:400px;",ui.render(ui.row({id:"grid",gutter:24},[
        ui.col({id:"grid-left",span:12},<div id:"left-content",style:"height:20px;","Left">)^,
        ui.col({id:"grid-right",span:12},<div id:"right-content",style:"height:20px;","Right">)^])^)>,
    <div style:"position:absolute;left:10px;top:450px;width:600px;",
        <button id:"sort-layout",type:"button",style:"display:inline-flex;align-items:center;gap:6px;text-align:center;font-family:Ahem;font-size:10px;line-height:10px;padding:0;border:0;",
            *["Word",<span id:"sort-layout-arrow","!">]>>
])^)^
