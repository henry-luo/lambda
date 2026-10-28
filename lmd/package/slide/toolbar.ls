// D7.2.5: reuse the packaged icon catalog for retained player controls.
import icons: lambda.ui.dtna.icons
import dom

let graphics = {previous:"step-backward",play:"caret-right",pause:"pause",resume:"play-circle",next:"step-forward",restart:"reload",
    console:"desktop",overview:"appstore",go:"arrow-right",blackout:"eye-invisible",pointer:"aim",fullscreen:"fullscreen",toolbar:"down"}
let shortcuts = {previous:"Left Arrow",next:"Right Arrow / Space",console:"P",overview:"O",blackout:"B",pointer:"L",fullscreen:"F",toolbar:"H"}
fn icon(name,cls="") => icons.render({name:name,width:18,height:18,class:cls})^
pub fn hint(command,label) => label ++ (if (shortcuts[command] == null) "" else " (" ++ shortcuts[command] ++ ")")
pub fn button(command,label,attribute="data-slide-tool",extra={},cls="") => <button type:"button",class:"slide-icon-button " ++ cls,
    [attribute]:command,'aria-label':label,title:hint(command,label),
    *:if (command in ["console","overview","blackout","pointer","fullscreen"]) {'aria-pressed':"false"} else {},*:extra,
    *if (command == "fullscreen") [icon("fullscreen","slide-enter-fullscreen"),icon("fullscreen-exit","slide-exit-fullscreen")]
    else [icon(graphics[command])];
    <span class:"slide-tooltip-label",'aria-hidden':"true",hint(command,label)>>

pub pn hide_hint(owner) {
    let popup = dom.query_selector(owner,".slide-tooltip");
    if (popup!=null) dom.style_set_property(popup,"display","none")
}
pub pn show_hint(owner,target) {
    let popup = dom.query_selector(owner,".slide-tooltip")
    let button = dom.closest(target,".slide-controls button.slide-icon-button")
    let label = dom.get_attribute(button,"title")
    let source = dom.query_selector(button,".slide-tooltip-label");
    if (popup==null) { return null }
    if (button==null or source==null or label==null) { hide_hint(owner); return null }
    let bar = dom.bounding_box(dom.query_selector(owner,".slide-controls"))
    let box = dom.bounding_box(button)
    let size = dom.bounding_box(source)
    // hidden labels retain text metrics; the visible popup lives outside the scrolling row.
    let x = max(0.0,min(bar.width-size.width,box.x-bar.x+(box.width-size.width)/2.0))
    dom.set_text_content(popup,label)
    dom.style_set_property(popup,"left",string(x) ++ "px")
    dom.style_set_property(popup,"display","block")
}
