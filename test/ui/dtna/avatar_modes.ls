import ui: lambda.ui.dtna
apply(<dtna.page tokens:{font_family:"Liberation Sans"}, <main style:"display:flex;flex-direction:column;align-items:flex-start;gap:12px;",*[for (node in [
    <dtna.avatar id:"avatar-default",label:"Ada", "A">,
    <dtna.avatar id:"avatar-small",size:'small',shape:'square', "A">,
    <dtna.avatar id:"avatar-large",size:'large',shape:'square',icon:<dtna.icon name:"user">>,
    <dtna.avatar id:"avatar-number",size:64, "A">,
    <dtna.avatar id:"avatar-icon",size:56,icon:<dtna.icon name:"user">>,
    <dtna.avatar id:"avatar-responsive",size:{xs:24,md:48,lg:64}, "A">,
    <dtna.avatar id:"avatar-custom",src:<span style:"color:black;","Custom">,dimension:72>
]) apply(node)]>>)
