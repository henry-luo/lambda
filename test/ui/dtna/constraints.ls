import ui: lambda.ui.dtna
import c: lambda.ui.core.component
import dom
view dtna_constraints_test: <constraints_test> state locked:true,activations:0,flags:"" {
    <div *[<button id:"unlock",type:"button","Toggle disabled">,
        <button id:"guarded",type:"button",*:c.boolean_attr("disabled",locked),"Activate">,
        <input id:"field",type:"text",*:c.boolean_attr("readonly",locked),*:c.boolean_attr("required",not locked)>,
        <button id:"inspect",type:"button","Inspect constraints">,<span id:"count",string(activations)>,<span id:"flags",flags>]>
}
on click(evt) {
    if (dom.closest(evt.target,"#unlock") != null) { locked = not locked }
    else if (dom.closest(evt.target,"#guarded") != null) { activations = activations + 1 }
    else if (dom.closest(evt.target,"#inspect") != null) {
        let owner = dom.closest(evt.target,".dtna-root")
        let field = dom.query_selector(owner,"#field")
        flags = string(dom.get_state(dom.query_selector(owner,"#guarded"),"disabled")) ++ ":" ++
            string(dom.get_state(field,"readonly")) ++ ":" ++ string(dom.get_state(field,"required"))
    }
}
ui.page(<constraints_test>)^
