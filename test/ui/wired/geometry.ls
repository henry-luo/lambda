import ui: lambda.ui.wired

view wired_geometry: <wired_geometry> state expanded:false {
    ui.render(<wired.flex direction:'vertical',align:"flex-start",gap:16,
        <wired.button id:"grow",style:"width:200px;","Grow card">
        <wired.badge id:"badge",style:"width:120px;height:30px;","Draft">
        <wired.card id:"card",style:"width:240px;",seed:31,
            <div style:"height:" ++ (if (expanded) "80" else "40") ++ "px;","Body">>
        <wired.input id:"field",style:"width:180px;",label:"Field">
        <wired.flex align:"stretch",style:"width:500px;",
            <wired.card id:"stretched",style:"width:240px;",<div style:"height:40px;","Short">>
            <div style:"width:100px;height:180px;","Tall">>
        <wired.progress id:"progress",percent:50,style:"width:240px;",show_info:false,label:"Progress">>)^
}
on ui_action(action) { if (action.id == "grow") { expanded = not expanded } }
ui.page(<wired_geometry>,{tokens:{font_family:"Arial"}})^
