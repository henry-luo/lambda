import dom
import focus: lambda.ui.core.focus

view <dialog_focus_test> () any^ {
    <main
        <section id:"dialog-one",role:"dialog",
            <button id:"one-first",type:"button","First">;
            <button id:"one-disabled",type:"button",disabled:"","Disabled">;
            <button id:"one-hidden",type:"button",style:"display:none;","Hidden">;
            <input id:"one-input",type:"text">;
            <button id:"one-last",type:"button","Last">>
        <section id:"dialog-two",role:"dialog",
            <button id:"two-first",type:"button","First">;
            <button id:"two-last",type:"button","Last">>>
}
on keydown(evt) {
    let dialog = dom.closest(evt.target,"[role=dialog]")
    if (dialog!=null) {
        let action = focus.dialog_key(dialog,evt)^
        if (action=='handled') { return 'prevent-default' }
        if (action=='dismiss') { dom.set_attribute(dialog,"data-dismiss-requested","true"); return 'prevent-default' }
    }
    return 'pass'
}

<html <body apply(<dialog_focus_test>)>>
