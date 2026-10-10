import dom

// author redraws retire the text hit while the native form activation is pending (ES25/ES26).
view activation_examples: <activation_examples> state clicks:0,submits:0,resets:0 {
    <main *[
        <form id:"activation-form",*[
            <input id:"activation-draft",value:"seed">,
            <button id:"activation-submit",type:"submit",<span "Submit " ++ string(clicks)>>,
            <button id:"activation-reset",type:"reset",<span "Reset " ++ string(clicks)>>]>,
        <output id:"activation-clicks",string(clicks)>,
        <output id:"activation-submits",string(submits)>,
        <output id:"activation-resets",string(resets)>]>
}
on click(evt) {
    if (dom.closest(evt.target,"button") != null) { clicks = clicks + 1 }
    'pass'
}
on submit(evt) { submits = submits + 1; 'prevent-default' }
on reset(evt) { resets = resets + 1; 'pass' }
<html <body apply(<activation_examples>)>>
