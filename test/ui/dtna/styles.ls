import ui: lambda.ui.dtna
apply(<dtna.page <dtna.flex direction:'vertical',align:"flex-start", *[
    <dtna.button id:"blue",variant:'primary', "Blue">,
    <dtna.config_provider tokens:{primary:"#722ed1"}, <dtna.space *[
        <dtna.button id:"purple",variant:'primary', "Purple">,
        <dtna.config_provider tokens:{radius:0}, <dtna.button id:"square",variant:'primary', "Square">>
    ]>>,
    <dtna.button id:"blue-again",variant:'primary',size:'large', "Blue again">,
    <dtna.alert id:"notice",closable:true,message:"Dismiss me">,
    <dtna.result id:"outcome",status:'success',title:"Ready">,
    <dtna.config_provider direction:'rtl', <dtna.config_provider tokens:{radius:0}, <dtna.button id:"rtl-child", "Inherited direction">>>,
    <dtna.icon id:"sized-icon",name:"check",width:24,height:18>,
    <div style:"width:400px;",apply(<dtna.row id:"grid",gutter:24, *[
        <dtna.col id:"grid-left",span:12, <div id:"left-content",style:"height:20px;","Left">>,
        <dtna.col id:"grid-right",span:12, <div id:"right-content",style:"height:20px;","Right">>]>)>,
    <div style:"position:absolute;left:10px;top:450px;width:600px;",
        <button id:"sort-layout",type:"button",style:"display:inline-flex;align-items:center;gap:6px;text-align:center;font-family:Ahem;font-size:10px;line-height:10px;padding:0;border:0;",
            *["Word",<span id:"sort-layout-arrow","!">]>>
]>>)
