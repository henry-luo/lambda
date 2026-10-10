import ui: lambda.ui.dtna
let models = [
    ui.rate({id:"rate-default",label:"Default rating"})^,
    ui.rate({id:"rate-half",allow_half:true,default_value:2.5,name:"rating",label:"Half rating"})^,
    ui.rate({id:"rate-fixed",value:2,label:"Controlled rating"})^,
    ui.rate({id:"rate-disabled",disabled:true,default_value:3})^,
    ui.rate({id:"rate-readonly",readonly:true,default_value:3})^,
    ui.rate({id:"rate-no-clear",allow_clear:false,default_value:2})^,
    ui.rate({id:"rate-no-keyboard",keyboard:false,default_value:2})^,
    ui.rate({id:"rate-small",size:'small',default_value:1})^,
    ui.rate({id:"rate-large",size:'large',default_value:4})^,
    ui.rate({id:"rate-custom",count:3,character:(star) => string(star.index+1),tooltips:["Poor","Fair","Good"]})^]
let rtl_model = ui.rate({id:"rate-rtl",allow_half:true,default_value:2})^
let inherited_model = ui.rate({id:"rate-inherited"})^
view rate_examples: <rate_examples> state count:0,request:"",requested_type:"",hover_request:"" {
    <main style:"display:flex;flex-direction:column;align-items:flex-start;gap:16px;",*[*ui.render(models),
        <div dir:"rtl",ui.render(rtl_model)>,
        <fieldset disabled:"",ui.render(inherited_model)>,
        <output id:"rate-count",string(count)>,<output id:"rate-request",request>,
        <output id:"rate-type",requested_type>,<output id:"rate-hover",hover_request>]>
}
on ui_change(action) {
    count = count + 1
    request = action.id ++ ":" ++ string(action.value)
    requested_type = string(type(action.value))
}
on ui_hover(action) { hover_request = action.id ++ ":" ++ string(action.value) }
ui.page(<rate_examples>,{tokens:{font_family:"Liberation Sans"}})^
