import ui: lambda.ui.dtna
view font_scopes: <font_scopes> state font_size:16 {
    <main style:"width:500px;",*ui.render([
        ui.title({id:"font-default"},"Default")^,
        ui.config_provider({tokens:{font_size:20,primary:"#722ed1"}},[
            ui.title({id:"font-large"},"Large")^,
            ui.title({id:"font-large-five",level:5},"Large five")^,
            ui.config_provider({tokens:{radius:0}},ui.title({id:"font-inherited"},"Inherited")^)^,
            ui.config_provider({tokens:{font_size:12}},[
                ui.title({id:"font-small"},"Small")^,
                ui.title({id:"font-small-five",level:5},"Small five")^])^])^,
        ui.config_provider({tokens:{font_size:font_size}},[
            ui.title({id:"font-dynamic",level:3},"Dynamic")^,
            ui.input({id:"font-draft",default_value:"seed"})^])^,
        ui.title({id:"font-sibling",level:5},"Sibling")^])>
}
on ui_change(action) {
    if (action.id == "font-draft") { font_size = if (len(action.value) > 4) 20 else 16 }
}
ui.page(<font_scopes>,{tokens:{font_family:"Liberation Sans"}})^
