import dom
import controls: lambda.ui.controls
import ant: lambda.ui.dtna
import bold: lambda.ui.bold
import wired: lambda.ui.wired

view <control_fixture> state text:"seed", notes:"note", choice:10, enabled:false, radio:"a",
    requests:0, accepted:true, value_type:"", compositions:0 {
    <body <style "body{font-family:Arial;padding:20px}input,textarea,select,button{margin:6px} .row{margin:10px}">
        <div class:"row", apply(<control.input id:"field",value:text,on_change:"text">)
            apply(<control.input id:"uncontrolled",default_value:"draft">)>
        <div class:"row", apply(<control.text_area id:"notes",value:notes,on_change:"notes">)>
        <div class:"row", apply(<control.select id:"choice",value:choice,
            options:[{value:10,label:"Ten"},{value:20,label:"Twenty"},{value:1.5,label:"Fraction"}],
            on_change:{action:"choice",param:"choice"}>)>
        <div class:"row", apply(<control.checkbox id:"enabled",checked:enabled,on_change:"enabled","Enabled">)
            apply(<control.radio id:"radio-a",name:"group",value:"a",checked:radio=="a",on_change:"radio","A">)
            apply(<control.radio id:"radio-b",name:"group",value:"b",checked:radio=="b",on_change:"radio","B">)>
        <div class:"row", apply(ant.input({id:"ant-field",value:text,on_change:"text"})^)
            apply(bold.input({id:"bold-field",value:text,on_change:"text"})^)
            apply(wired.input({id:"wired-field",value:text,on_change:"text"})^)>
        <button id:"programmatic","Change state"> <button id:"reject","Reject edits">
        <span id:"requests",string(requests)> <span id:"type",value_type> <span id:"compositions",string(compositions)>
    >
}
on control_change(req) {
    requests = requests + 1
    value_type = string(type(req.value))
    if (accepted) {
        if (req.action == "text") { text = req.value }
        else if (req.action == "notes") { notes = req.value }
        else if (req.action == "choice") { choice = req.params.choice }
        else if (req.action == "enabled") { enabled = req.value }
        else if (req.action == "radio") { radio = req.value }
    }
}
on compositionupdate(evt) {
    // redraw a parent during preedit; the binding must leave the IME alone.
    compositions = compositions + 1
}
on click(evt) {
    let id = dom.get_attribute(evt.target,"id")
    if (id == "programmatic") {
        text = "program"
        notes = "other"
        choice = 20
        enabled = true
        radio = "b"
    }
    else if (id == "reject") { accepted = false }
    'pass'
}
<html <head <title "Controlled binding lifecycle">> apply(<control_fixture>)>
