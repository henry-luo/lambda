import ui: lambda.ui.dtna
apply(<dtna.page tokens:{font_family:"Liberation Sans"}, <main style:"width:360px;",*[for (node in [
    <dtna.skeleton id:"loading",avatar:{size:48},paragraph:{rows:2,width:["100%","60%"]}>,
    <dtna.skeleton id:"ready",loading:false, "Ready content">,
    <dtna.space style:"margin-top:24px;", *[
        <dtna.skeleton_avatar id:"avatar",dimension:48>,
        <dtna.skeleton_button id:"button">,
        <dtna.skeleton_input id:"input">]>,
    <dtna.skeleton_image id:"image",style:"margin-top:24px;">,
    <dtna.skeleton_node id:"node",width:80,height:80, "Chart">
]) apply(node)]>>)
