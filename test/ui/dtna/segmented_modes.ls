import ui: lambda.ui.dtna
let square = <svg width:14,height:14,viewBox:"0 0 14 14",<rect x:2,y:2,width:10,height:10,fill:"currentColor">>
let items = [{key:1,label:"One",icon:square},{key:2,label:"Skip",disabled:true},{key:3,label:"Three",icon:square}]
let models = [
    ui.segmented({id:"seg-block",items:items,block:true,style:"width:300px;"})^,
    ui.segmented({id:"seg-vertical",items:items,orientation:'vertical',style:"width:180px;"})^,
    ui.segmented({id:"seg-small",items:items,block:true,size:'small',style:"width:240px;"})^,
    ui.segmented({id:"seg-large",items:items,block:true,size:'large',style:"width:320px;"})^,
    ui.segmented({id:"seg-round",items:items,block:true,shape:'round',style:"width:240px;"})^,
    ui.config_provider({direction:'rtl'},ui.segmented({id:"seg-rtl",items:items,block:true,style:"width:300px;"})^)^,
    ui.segmented({id:"seg-fixed",items:items,value:1})^,
    ui.form({id:"seg-form"},[
        ui.segmented({id:"seg-named",items:items,name:"choice"})^,
        ui.segmented({id:"seg-disabled",items:items,name:"ignored",disabled:true})^,
        ui.button({id:"seg-submit",type:"submit"},"Submit")^])^]
view segmented_examples: <segmented_examples> state count:0, request:"", requested_type:"", entries:"" {
    <main style:"display:flex;flex-direction:column;align-items:flex-start;gap:12px;",*[
        *ui.render(models),<output id:"seg-count",string(count)>,<output id:"seg-request",request>,
        <output id:"seg-type",requested_type>,<output id:"seg-entries",entries>]>
}
on ui_change(action) {
    count = count + 1
    request = action.id ++ ":" ++ string(action.value)
    requested_type = string(type(action.value))
}
on ui_action(action) {
    if (action.action == 'submit') { entries = join([for (entry in action.value) entry[0] ++ ":" ++ entry[1]],",") }
}
ui.page(<segmented_examples>,{tokens:{font_family:"Liberation Sans"}})^
