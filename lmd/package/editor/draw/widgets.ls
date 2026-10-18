// host-independent controls; the owning canvas handles their declarative actions.
import shapes: .shapes
pub fn button(action,label) => <button type:"button",'data-draw-action':action,label>
pub fn toolbar() => <div class:"draw-toolbar",role:"toolbar",'aria-label':"Drawing tools",
    *[for (item in [{id:"select",label:"Select"},{id:"connect",label:"Connect"},
        {id:"undo",label:"Undo"},{id:"redo",label:"Redo"},{id:"duplicate",label:"Duplicate"},{id:"delete",label:"Delete"},
        {id:"zoom-in",label:"Zoom in"},{id:"zoom-out",label:"Zoom out"}]) button(item.id,item.label)]>
pub fn palette() => <div class:"draw-palette",role:"toolbar",'aria-label':"Shapes",
    *[for (entry in shapes.catalog) button("add-" ++ entry.id,entry.label)]>
pub fn inspector(prefix) => <div class:"draw-inspector",
    *[for (entry in [{key:"label",label:"Label",kind:"text"},{key:"rotate",label:"Rotation",kind:"number"}])
        <label 'for':prefix ++ "field-" ++ entry.key,entry.label;
            <input id:prefix ++ "field-" ++ entry.key,type:entry.kind,'data-draw-property':entry.key>>]>
