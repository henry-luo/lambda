import ui: lambda.ui.dtna
let models = [
    <dtna.tag id:"tag-default", "Default">,
    <dtna.tag id:"tag-blue",color:'blue',variant:'outlined', "Blue">,
    <dtna.tag id:"tag-red",color:'red',variant:'solid', "Solid">,
    <dtna.tag id:"tag-success",color:'success',variant:'outlined',icon:<dtna.icon name:"check-circle">, "Success">,
    <dtna.tag id:"tag-custom",color:"#108ee9", "Custom">,
    <dtna.tag id:"tag-close",closable:true, "Close">,
    <dtna.tag id:"tag-retain",closable:true,visible:true, "Retain">,
    <dtna.tag id:"tag-disabled",closable:true,disabled:true,color:'red', "Disabled">,
    <dtna.tag id:"tag-link",href:"#target", "Link">,
    <dtna.checkable_tag id:"tag-check", "Check">,
    <dtna.checkable_tag id:"tag-fixed",checked:false, "Fixed">,
    <dtna.checkable_tag id:"tag-check-disabled",disabled:true,checked:true, "Disabled checked">,
    <dtna.config_provider disabled:true, <dtna.checkable_tag id:"tag-inherited", "Inherited">>]
view tag_examples: <tag_examples> state count:0,request:"",requested_type:"" {
    <main style:"display:flex;flex-direction:column;align-items:flex-start;gap:12px;",*[
        *[for (node in models) apply(node)],<div id:"target","Target">,<output id:"tag-count",string(count)>,
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
apply(<dtna.page tokens:{font_family:"Liberation Sans"}, <tag_examples>>)
