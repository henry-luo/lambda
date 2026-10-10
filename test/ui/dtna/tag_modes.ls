import ui: lambda.ui.dtna
let models = [
    ui.tag({id:"tag-default"},"Default")^,
    ui.tag({id:"tag-blue",color:'blue',variant:'outlined'},"Blue")^,
    ui.tag({id:"tag-red",color:'red',variant:'solid'},"Solid")^,
    ui.tag({id:"tag-success",color:'success',variant:'outlined',icon:ui.icon({name:"check-circle"})^},"Success")^,
    ui.tag({id:"tag-custom",color:"#108ee9"},"Custom")^,
    ui.tag({id:"tag-close",closable:true},"Close")^,
    ui.tag({id:"tag-retain",closable:true,visible:true},"Retain")^,
    ui.tag({id:"tag-disabled",closable:true,disabled:true,color:'red'},"Disabled")^,
    ui.tag({id:"tag-link",href:"#target"},"Link")^,
    ui.checkable_tag({id:"tag-check"},"Check")^,
    ui.checkable_tag({id:"tag-fixed",checked:false},"Fixed")^,
    ui.checkable_tag({id:"tag-check-disabled",disabled:true,checked:true},"Disabled checked")^,
    ui.config_provider({disabled:true},ui.checkable_tag({id:"tag-inherited"},"Inherited")^)^]
view tag_examples: <tag_examples> state count:0,request:"",requested_type:"" {
    <main style:"display:flex;flex-direction:column;align-items:flex-start;gap:12px;",*[
        *ui.render(models),<div id:"target","Target">,<output id:"tag-count",string(count)>,
        <output id:"tag-request",request>,<output id:"tag-type",requested_type>]>
}
on ui_change(action) {
    count = count + 1
    request = action.id ++ ":" ++ string(action.value)
    requested_type = string(type(action.value))
}
on ui_action(action) {
    count = count + 1
    request = action.id ++ ":" ++ string(action.action)
}
ui.page(<tag_examples>,{tokens:{font_family:"Liberation Sans"}})^
