// Document icons share the ribbon's outline artwork; emojis remain Unicode text.
import icons: ~~.mod_icons
import objects: .mod_objects
import colors: lambda.ui.core.component

pub let catalog = [
    {id:"heart",label:"Heart"},{id:"star",label:"Star"},{id:"emoji",label:"Smile"},{id:"check",label:"Check"},
    {id:"places",label:"Location"},{id:"globe",label:"Globe"},{id:"book",label:"Book"},{id:"document",label:"Document"},
    {id:"folder",label:"Folder"},{id:"album",label:"Picture"},{id:"film",label:"Film"},{id:"sound",label:"Sound"},
    {id:"comment",label:"Message"},{id:"info",label:"Information"},{id:"search",label:"Search"},{id:"bookmark",label:"Bookmark"},
    {id:"chart",label:"Chart"},{id:"smartart",label:"Organization"},{id:"library",label:"Grid"},{id:"link",label:"Link"}]
pub let emojis = [
    {label:"Faces",chars:["😀","😊","😂","😍","🤔","😎","🥳","😢"]},
    {label:"Gestures & symbols",chars:["👍","👋","👏","🙏","❤️","⭐","✅","💡"]},
    {label:"Nature & objects",chars:["🌱","🌸","🌞","🌈","🐱","🚀","🎨","📚"]}]
pub let defaults = {name:"star",color:"#2563eb",width:64,height:64,rotation:0}
fn valid(p) => contains([for (entry in catalog) entry.id],p.name) and colors.hex_color(p.color) and objects.dimensions_valid(p)
pub fn settings(n) {
    let p = objects.settings(n,"data-word-icon",defaults);
    if (p!=null and valid(p)) p else null
}
pub fn graphic(p) => <svg xmlns:"http://www.w3.org/2000/svg",width:p.width,height:p.height,viewBox:"0 0 24 24",icons.glyph(p.name,p.color)>
pub fn transaction(editor,params,selected=null) map^ {
    let p = {*:defaults,*:params}
    if (not valid(p)) raise error("Word: choose an icon, a #RRGGBB color, width 16–650 px, height 16–2000 px, and rotation −180–180°")
    else objects.transaction(editor,"data-word-icon",p,graphic(p),p.width,p.height,
        [for (entry in catalog where entry.id==p.name) entry.label][0],selected)^
}
