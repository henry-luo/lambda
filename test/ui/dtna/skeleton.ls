import ui: lambda.ui.dtna
ui.page(<main style:"width:360px;",*ui.render([
    ui.skeleton({id:"loading",avatar:{size:48},paragraph:{rows:2,width:["100%","60%"]}})^,
    ui.skeleton({id:"ready",loading:false},"Ready content")^,
    ui.space({style:"margin-top:24px;"},[
        ui.skeleton_avatar({id:"avatar",dimension:48})^,
        ui.skeleton_button({id:"button"})^,
        ui.skeleton_input({id:"input"})^])^,
    ui.skeleton_image({id:"image",style:"margin-top:24px;"})^,
    ui.skeleton_node({id:"node",width:80,height:80},"Chart")^
])>,{tokens:{font_family:"Liberation Sans"}})^
