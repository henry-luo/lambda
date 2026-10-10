import ui: lambda.ui.dtna
let models = [
    ui.alert({id:"alert-default",message:"Info"})^,
    ui.alert({id:"alert-success",title:"Success",status:'success',show_icon:true})^,
    ui.alert({id:"alert-warning",title:"Warning",status:'warning',description:"Review the details",show_icon:true})^,
    ui.alert({id:"alert-error",title:"Error",status:'error',variant:'filled',closable:true})^,
    ui.alert({id:"alert-banner",title:"Banner",banner:true})^,
    ui.alert({id:"alert-custom",title:"Custom",show_icon:true,icon:"!",action:ui.button({id:"alert-retry",size:'small'},"Retry")^})^,
    ui.alert({id:"alert-retain",title:"Retain",visible:true,closable:true,close_icon:"Keep",close_label:"Keep notice"})^,
    ui.alert({id:"alert-parts",title:"Styled",closable:true,class_names:{title:"authored-title",close:"authored-close"},styles:{title:"font-weight:700;",root:"border-radius:0;"}})^,
    ui.alert({class:"alert-outer",title:"Outer",closable:true},ui.alert({class:"alert-inner",title:"Inner",closable:true})^)^]
let rtl_model = ui.alert({id:"alert-rtl",title:"RTL",description:"Right to left",show_icon:true,closable:true})^
view alert_examples: <alert_examples> state count:0,request:"" {
    <main style:"width:400px;display:flex;flex-direction:column;gap:12px;",*[*ui.render(models),
        <div dir:"rtl",ui.render(rtl_model)>,<output id:"alert-count",string(count)>,<output id:"alert-request",request>]>
}
on ui_action(action) { count = count + 1; request = string(action.id) ++ ":" ++ string(action.action) }
ui.page(<alert_examples>,{tokens:{font_family:"Liberation Sans"}})^
