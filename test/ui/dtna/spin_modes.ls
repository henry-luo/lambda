import ui: lambda.ui.dtna
import dom
let models = [
    <dtna.spin id:"spin-default">,
    <dtna.spin id:"spin-small",size:'small',description:"Small">,
    <dtna.spin id:"spin-large",size:'large',description:"Large">,
    <dtna.spin id:"spin-idle",spinning:false, <button id:"idle-content","Content">>,
    <dtna.spin id:"spin-nested",description:"Loading",style:"width:400px;", <div style:"height:100px;",<button id:"blocked-content","Content">>>,
    <dtna.spin id:"spin-manual",percent:37>,
    <dtna.spin id:"spin-custom",indicator:<b id:"custom-indicator","Custom">>,
    <dtna.spin id:"spin-parts",description:"Styled",class_names:{section:"authored-section",indicator:"authored-indicator"},
        styles:{description:"font-weight:700;"}>,
    <dtna.spin id:"spin-delay",delay:5000,description:"Delayed">,
    <dtna.spin id:"spin-auto",percent:'auto'>]
let actions = [<dtna.button id:"toggle-spin", "Toggle">,<dtna.button id:"remove-spin", "Remove">,
    <dtna.button id:"toggle-fullscreen", "Fullscreen">]
view spin_examples: <spin_examples> state spinning:false,count:0,removed:false,fullscreen:false {
    <main style:"width:400px;display:flex;flex-direction:column;align-items:flex-start;gap:12px;",
        *[*[for (node in models) apply(node)],
        apply(<dtna.spin id:"spin-controlled",spinning:spinning,delay:300, "Controlled content">),
        *[for (node in actions) apply(node)],
        apply(<dtna.spin id:"spin-fullscreen",fullscreen:true,spinning:fullscreen,description:"Fullscreen">),
        if (not removed) apply(<dtna.spin id:"spin-removable",delay:60000>) else null,
        <output id:"spin-clicks",string(count)>]>
}
on ui_action(action) {
    if (action.id == "toggle-spin") { spinning = not spinning }
    if (action.id == "remove-spin") { removed = true }
    if (action.id == "toggle-fullscreen") { fullscreen = not fullscreen }
}
on click(evt) { if (dom.get_attribute(evt.target,"id") == "idle-content" or dom.get_attribute(evt.target,"id") == "blocked-content") { count = count + 1 } }
apply(<dtna.page tokens:{font_family:"Liberation Sans"}, <spin_examples>>)
