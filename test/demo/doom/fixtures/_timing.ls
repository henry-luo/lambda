// CSS timing tasks must retain Lambda author handlers when no JS realm exists.
import dom

pn note(evt, attribute) {
    let owner = dom.closest(evt.target, "#timing")
    if (owner != null) dom.set_attribute(owner, attribute,
        string(int(dom.get_attribute(owner, attribute) or "0") + 1))
}
view <timing_probe> {
    <div id:"timing", 'data-start':"0", 'data-iteration':"0", 'data-end':"0",
        <div id:"animated", style:"width:20px;height:20px;background:red;animation:pulse .1s linear 3;">>
}
on animationstart(evt) { note(evt, "data-start"); return 'handled' }
on animationiteration(evt) { note(evt, "data-iteration"); return 'handled' }
on animationend(evt) { note(evt, "data-end"); return 'handled' }
<html <head <style "@keyframes pulse{from{opacity:.2}to{opacity:1}}">>
    <body apply(<timing_probe>)>>
