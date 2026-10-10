import ui: lambda.ui.dtna
ui.page(ui.space({wrap:true},[
    ui.button({variant:'primary'},"Primary")^,ui.button({},"Default")^,
    ui.button({variant:'dashed'},"Dashed")^,ui.button({variant:'text'},"Text")^,
    ui.button({variant:'link'},"Link")^,ui.button({disabled:true},"Disabled")^,
    ui.button({danger:true},"Danger")^,ui.button({loading:true},"Loading")^
])^,{tokens:{font_family:"Liberation Sans"}})^
