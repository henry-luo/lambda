import dom

view focus_field: <focus_field> {
    if (~.multiline) <textarea id:~.field_id,class:"shared-field",~.value>
    else <input id:~.field_id,class:"shared-field",value:~.value>
}
on input(evt) { emit("field_change",{id:~.field_id,value:dom.get_state(evt.target,"value")}) }
view focus_form: <focus_form> state entry:"", notes:"abcd" {
    <main style:"display:flex;flex-direction:column;align-items:flex-start;gap:12px;", *[
        apply(<focus_field field_id:"first-entry",value:"fixed">),
        apply(<focus_field field_id:"edited-entry",value:entry>),
        apply(<focus_field field_id:"first-notes",multiline:true,value:"fixed notes">),
        apply(<focus_field field_id:"edited-notes",multiline:true,value:notes>),
        <output id:"entry-value",entry>,<output id:"notes-value",notes>]>
}
on field_change(action) {
    if (action.id == "edited-entry") { entry = action.value }
    if (action.id == "edited-notes") { notes = action.value }
}
<html <body style:"margin:24px;",apply(<focus_form>)>>
