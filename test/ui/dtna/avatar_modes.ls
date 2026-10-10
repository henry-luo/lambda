import ui: lambda.ui.dtna
ui.page(<main style:"display:flex;flex-direction:column;align-items:flex-start;gap:12px;",*ui.render([
    ui.avatar({id:"avatar-default",label:"Ada"},"A")^,
    ui.avatar({id:"avatar-small",size:'small',shape:'square'},"A")^,
    ui.avatar({id:"avatar-large",size:'large',shape:'square',icon:ui.icon({name:"user"})^})^,
    ui.avatar({id:"avatar-number",size:64},"A")^,
    ui.avatar({id:"avatar-icon",size:56,icon:ui.icon({name:"user"})^})^,
    ui.avatar({id:"avatar-responsive",size:{xs:24,md:48,lg:64}},"A")^,
    ui.avatar({id:"avatar-custom",src:<span style:"color:black;","Custom">,dimension:72})^
])>,{tokens:{font_family:"Liberation Sans"}})^
