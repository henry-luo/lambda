import ui: lambda.ui.dtna
fn cell() => <div style:"height:20px;width:0;">
ui.page(<main id:"grid-main",style:"width:400px;",*ui.render([
    ui.row({id:"units-row",gutter:[{xs:"1rem",md:"24px"},"1em"]},[
        ui.col({id:"units-left",span:12},<div id:"units-inner",style:"height:20px;">)^,
        ui.col({span:12},cell())^])^,
    ui.row({id:"align-row",align:{xs:'top',md:'middle',lg:'bottom'},justify:{xs:'start',md:'center',lg:'end'},style:"height:80px;margin-top:16px;"},
        ui.col({id:"align-cell",span:6},cell())^)^,
    ui.row({id:"shift-row",style:"margin-top:16px;"},[
        ui.col({id:"push-cell",span:8,push:8,md:{push:0}},cell())^,
        ui.col({id:"pull-cell",span:8,pull:8,md:{pull:0}},cell())^])^,
    <div dir:"rtl",ui.render(ui.row({id:"rtl-shift-row",style:"margin-top:16px;"},[
        ui.col({id:"rtl-push",span:8,push:8,md:{push:0}},cell())^,
        ui.col({id:"rtl-pull",span:8,pull:8,md:{pull:0}},cell())^])^)>,
    ui.row({id:"flex-row",wrap:false,style:"margin-top:16px;"},[
        ui.col({id:"fixed-flex",flex:"100px"},cell())^,
        ui.col({id:"one-flex",flex:1},cell())^,
        ui.col({id:"two-flex",flex:2},cell())^])^,
    ui.row({id:"responsive-flex-row",wrap:false,style:"margin-top:16px;"},[
        ui.col({id:"responsive-flex-one",xs:{flex:"80px"},md:{flex:1}},cell())^,
        ui.col({id:"responsive-flex-three",xs:{flex:1},md:{flex:3}},cell())^])^,
    ui.row({id:"nested-row",gutter:16,style:"margin-top:16px;"},
        ui.col({id:"nested-col",span:12},ui.row({id:"nested-inner-row",gutter:8},[
            ui.col({id:"nested-inner-col",span:12},cell())^,ui.col({span:12},cell())^])^)^)^,
    ui.flex({id:"flex-container",gap:"1rem",style:"width:400px;margin-top:16px;"},[
        <div id:"flex-fixed",style:"width:80px;height:20px;">,
        ui.flex({id:"flex-growing",flex:1},cell())^])^
])>,{tokens:{font_family:"Liberation Sans"}})^
