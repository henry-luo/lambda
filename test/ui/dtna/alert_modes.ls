import ui: lambda.ui.dtna
let models = [
    <dtna.alert id:"alert-default",message:"Info">,
    <dtna.alert id:"alert-success",title:"Success",status:'success',show_icon:true>,
    <dtna.alert id:"alert-warning",title:"Warning",status:'warning',description:"Review the details",show_icon:true>,
    <dtna.alert id:"alert-error",title:"Error",status:'error',variant:'filled',closable:true>,
    <dtna.alert id:"alert-banner",title:"Banner",banner:true>,
    <dtna.alert id:"alert-custom",title:"Custom",show_icon:true,icon:"!",action:<dtna.button id:"alert-retry",size:'small', "Retry">>,
    <dtna.alert id:"alert-retain",title:"Retain",visible:true,closable:true,close_icon:"Keep",close_label:"Keep notice">,
    <dtna.alert id:"alert-parts",title:"Styled",closable:true,class_names:{title:"authored-title",close:"authored-close"},styles:{title:"font-weight:700;",root:"border-radius:0;"}>,
    <dtna.alert class:"alert-outer",title:"Outer",closable:true, <dtna.alert class:"alert-inner",title:"Inner",closable:true>>]
let rtl_model = <dtna.alert id:"alert-rtl",title:"RTL",description:"Right to left",show_icon:true,closable:true>
view alert_examples: <alert_examples> state count:0,request:"" {
    <main style:"width:400px;display:flex;flex-direction:column;gap:12px;",*[*[for (node in models) apply(node)],
        <div dir:"rtl",apply(rtl_model)>,<output id:"alert-count",string(count)>,<output id:"alert-request",request>]>
}
on ui_action(action) { count = count + 1; request = string(action.id) ++ ":" ++ string(action.action) }
apply(<dtna.page tokens:{font_family:"Liberation Sans"}, <alert_examples>>)
