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
    ui.icon({id:"sized-icon",name:"check",width:24,height:18})^
])^)^
