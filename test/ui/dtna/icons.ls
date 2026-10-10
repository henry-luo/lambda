import ui: lambda.ui.dtna
ui.page(ui.space({gap:24},[
    ui.icon({id:"outlined-icon",name:"check-circle",label:"Complete",width:32,height:32})^,
    ui.icon({id:"filled-icon",name:"check-circle",theme:'filled',width:32,height:32})^,
    ui.icon({id:"two-tone-icon",name:"check-circle",theme:'two-tone',primary_color:"#722ed1",secondary_color:"#f9f0ff",width:32,height:32})^,
    ui.icon({id:"rotated-icon",name:"arrow-up",rotate:90,width:32,height:32})^,
    ui.icon({id:"group-icon",name:"dot-net",width:32,height:32})^,
    ui.icon({id:"spinner-icon",name:"loading",width:32,height:32})^
])^)^
