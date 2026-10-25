// D7.2.5: reuse the packaged icon catalog for retained player controls.
import icons: lambda.ui.dtna.icons

let graphics = {previous:"step-backward",play:"caret-right",pause:"pause",resume:"play-circle",next:"step-forward",restart:"reload",
    console:"desktop",overview:"appstore",go:"arrow-right",blackout:"eye-invisible",pointer:"aim",fullscreen:"fullscreen",toolbar:"down"}
let shortcuts = {previous:"Left Arrow",next:"Right Arrow / Space",console:"P",overview:"O",blackout:"B",pointer:"L",fullscreen:"F",toolbar:"H"}
fn icon(name,cls="") => icons.render({name:name,width:18,height:18,class:cls})^
pub fn button(command,label,attribute="data-slide-tool",extra={},cls="") => <button type:"button",class:"slide-icon-button " ++ cls,
    [attribute]:command,'aria-label':label,title:label ++ (if (shortcuts[command] == null) "" else " (" ++ shortcuts[command] ++ ")"),
    *:if (command in ["console","overview","blackout","pointer","fullscreen"]) {'aria-pressed':"false"} else {},*:extra,
    *if (command == "fullscreen") [icon("fullscreen","slide-enter-fullscreen"),icon("fullscreen-exit","slide-exit-fullscreen")]
    else [icon(graphics[command])]>
