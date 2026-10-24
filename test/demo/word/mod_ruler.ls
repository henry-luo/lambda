// ruler gestures preview DOM geometry; only completed commands enter source history (D7.2.5).
import dom
import page: .mod_page
import columns: .mod_columns
import model: .mod_model
import stories: .mod_stories
import navigation: .mod_navigation
import controller: lambda.edit.controller
import lambda.editor.mod_doc

let sides = [{id:"left",opposite:"right",axis:"width",vertical:false,sign:1},
    {id:"right",opposite:"left",axis:"width",vertical:false,sign: -1},
    {id:"top",opposite:"bottom",axis:"height",vertical:true,sign:1},
    {id:"bottom",opposite:"top",axis:"height",vertical:true,sign: -1}]
let indents = [{id:"first",label:"First line indent",path:"M1 1H11L6 7Z"},
    {id:"hanging",label:"Hanging indent",path:"M1 7L6 1L11 7Z"},
    {id:"indent",label:"Left indent",path:"M1 1H11V5H1Z"},
    {id:"right",label:"Right indent",path:"M1 7L6 1L11 7Z"}]
fn bounded(value,lo,hi) => max(lo,min(max(lo,hi),value))
fn rounded_tenth(value) => round(value*10)/10
pub fn margin_value(doc,side,delta,page_no=1) {
    let s = [for (s in sides where s.id==side) s][0]
    let p = page.resolved(page.settings(doc),page_no)
    let key = page.margin_key(doc,side,page_no)
    let required = stories.minimum_margin(doc,side)
    let rounded = ceil(required*10)/10
    // page padding rounds to whole pixels; the clamped endpoint must still fit its story.
    let low = if (round(rounded*96/25.4)<required*96/25.4) rounded+0.1 else rounded
    let minimum = if (s.vertical) 20 else columns.minimum_width(doc)
    let high = min(80,floor((page.dimension(p,s.axis)-p[s.opposite]-minimum-0.01)*10)/10);
    if (s==null) {} else map([(key,if (delta==0) p[side] else bounded(rounded_tenth(p[side]+delta*s.sign*25.4/96),low,high))])
}
// all paragraph values are points; screen movement and available line width are CSS pixels.
pub fn indent_value(p,width,handle,delta) {
    let room = max(0,(width-20)*0.75)
    let shift = delta*0.75;
    if (handle=="first") {first:if (delta==0) p.first else bounded(rounded_tenth(p.first+shift),-72,min(200,room-p.indent-p.right))}
    else if (handle=="right") {right:if (delta==0) p.right else bounded(rounded_tenth(p.right-shift),0,min(200,room-p.indent-max(0,p.first)))}
    else if (handle=="hanging") {
        let absolute = p.indent+p.first
        let indent = if (delta==0) p.indent else bounded(rounded_tenth(p.indent+shift),max(0,absolute-200),min(200,min(room-p.right,absolute+72)));
        {indent:indent,first:rounded_tenth(absolute-indent)}
    } else {indent:if (delta==0) p.indent else bounded(rounded_tenth(p.indent+shift),0,min(200,room-p.right-max(0,p.first)))}
}
fn marks(extent,origin,zoom,vertical=false) {
    let step = 96/2.54/4;
    // retain offscreen ticks so a moving margin can rebase zero during its preview.
    [for (n in -int(ceil(extent/step)) to int(ceil(extent/step)))
        <span class:"ruler-mark" ++ (if (n%4==0) " ruler-number" else if (n%2==0) " ruler-half" else ""),
            style:(if (vertical) "top:" else "left:") ++ string((origin+n*step)*zoom) ++ "px",
            ["data-ruler-cm"]:string(n/4), if (n%4==0) string(abs(n/4)) else null>]
}
fn margin(size,zoom,vertical,trailing=false) =>
    <span class:"ruler-margin " ++ (if (trailing) "ruler-trailing" else "ruler-leading"),
        style:(if (vertical) "height:" else "width:") ++ string(size*zoom) ++ "px">
fn margin_name(p,side,page_no) => if (p.mirrored==true and not side.vertical)
    (if ((side.id=="left")==(max(1,page_no)%2==1)) "inside" else "outside") else side.id
fn margin_handle(p,side,position,disabled,page_no=0) =>
    <button type:"button",id:"ruler-margin-" ++ side.id ++ (if (page_no==0) "" else "-" ++ string(page_no)),
        class:"ruler-control ruler-margin-handle",["data-ruler-margin"]:side.id,*:if (disabled) {disabled:""} else {},
        style:(if (side.vertical) "top:" else "left:") ++ string(position) ++ "px",
        title:margin_name(p,side,page_no) ++ " page margin — drag to resize",["aria-label"]:margin_name(p,side,page_no) ++ " page margin",
        role:"slider",["aria-orientation"]:if (side.vertical) "vertical" else "horizontal",
        ["aria-valuemin"]:"0",["aria-valuemax"]:"80",["aria-valuenow"]:string(p[side.id]),["aria-valuetext"]:string(p[side.id]) ++ " mm">
pub fn horizontal(doc,zoom,disabled=false) {
    let g = page.geometry(doc)
    let p = page.settings(doc);
    <div id:"horizontal-ruler",class:"ruler horizontal-ruler",role:"group",["aria-label"]:"Horizontal ruler in centimeters",["data-page"]:"1",
        style:"width:" ++ string(g.width*zoom) ++ "px",
        margin(g.left,zoom,false); margin(g.right,zoom,false,true);
        *marks(g.width,g.left,zoom);
        *[for (s in sides where not s.vertical) margin_handle(p,s,(if (s.id=="left") g.left else g.width-g.right)*zoom,disabled)];
        *[for (s in indents) <button type:"button",id:"ruler-indent-" ++ s.id,class:"ruler-control ruler-indent ruler-indent-" ++ s.id,
            ["data-ruler-indent"]:s.id,*:if (disabled) {disabled:""} else {},style:"visibility:hidden",title:s.label ++ " — drag to adjust",
            ["aria-label"]:s.label,role:"slider",["aria-orientation"]:"horizontal",
            ["aria-valuemin"]:if (s.id=="first") "-72" else "0",["aria-valuemax"]:"200",
            <svg width:"12",height:"8",viewBox:"0 0 12 8",["aria-hidden"]:"true",<path d:s.path>>>]>
}
pub fn vertical(doc,zoom,disabled=false) {
    let g = page.geometry(doc)
    let p = page.settings(doc);
    <div id:"vertical-ruler",class:"vertical-ruler",role:"group",["aria-label"]:"Vertical ruler in centimeters",
        *[for (i,sheet in page.sheets(doc))
            <div class:"ruler-page",["data-page"]:string(i+1),style:"height:" ++ string(g.height*zoom) ++ "px",
                margin(g.top,zoom,true); margin(g.bottom,zoom,true,true);
                *marks(g.height,g.top,zoom,true);
                *[for (s in sides where s.vertical) margin_handle(p,s,(if (s.id=="top") g.top else g.height-g.bottom)*zoom,disabled,i+1)]>]>
}
fn pixels(node,key) => model.points(dom.computed_style(node,key))/0.75
fn block_nodes(node) => [for (n in dom.query_selector_all(controller.surface(dom.document_element(node)),model.block_selector)
    where dom.query_selector(n,model.block_selector)==null) n]
fn block_node(editor,node,path) {
    let i = [for (i,e in model.blocks(editor.doc) where e.path==path) i][0];
    block_nodes(node)[i]
}
pub fn paragraph_geometry(editor,node,zoom) {
    let entry = model.paragraph_block(editor)
    let target = block_node(editor,node,entry.path)
    let box = dom.bounding_box(target)
    let left = pixels(target,"margin-left")
    let right = pixels(target,"margin-right")
    let pad_left = pixels(target,"padding-left")+pixels(target,"border-left-width")
    let pad_right = pixels(target,"padding-right")+pixels(target,"border-right-width");
    if (target==null or box.width<=0) null else {path:entry.path,width:box.width/zoom+left+right-pad_left-pad_right,
        origin:box.x+(pad_left-left)*zoom,left:box.x+pad_left*zoom,right:box.x+box.width-pad_right*zoom,
        values:{indent:left*0.75,right:right*0.75,first:pixels(target,"text-indent")*0.75}}
}
// geometry refreshes often repeat; unchanged accessibility values need no DOM mutation.
pn attribute(node,key,value) {
    if (dom.get_attribute(node,key)!=value) dom.set_attribute(node,key,value)
}
pn indent_position(editor,node,zoom) {
    let root = dom.document_element(node)
    let ruler = dom.get_element_by_id(root,"horizontal-ruler")
    let box = dom.bounding_box(ruler)
    let g = paragraph_geometry(editor,node,zoom)
    for (s in indents) {
        let handle = dom.get_element_by_id(root,"ruler-indent-" ++ s.id)
        dom.style_set_property(handle,"visibility",if (g==null) "hidden" else "visible")
        if (g!=null) {
            let x = if (s.id=="right") g.right else g.left+if (s.id=="first") g.values.first/0.75*zoom else 0
            let value = if (s.id=="hanging") g.values.indent else g.values[s.id]
            dom.style_set_property(handle,"left",string(x-box.x) ++ "px")
            attribute(handle,"aria-valuenow",string(rounded_tenth(value)))
            attribute(handle,"aria-valuetext",string(rounded_tenth(value)) ++ " pt")
        }
    }
}
pub pn sync(node) {
    let root = dom.document_element(node)
    let chrome = dom.get_element_by_id(root,"canvas-chrome")
    dom.set_scroll_state(chrome,dom.scroll_state(node).x,0.0)
    let viewport = dom.bounding_box(node)
    let rulers = dom.query_selector_all(root,"#vertical-ruler .ruler-page")
    // page bounds include zoom, page gaps and content-driven height.
    for (i,sheet in dom.query_selector_all(root,"#edit-surface .word-page")) {
        let bounds = dom.bounding_box(sheet)
        dom.style_set_property(rulers[i],"top",string(bounds.y-viewport.y) ++ "px")
        dom.style_set_property(rulers[i],"height",string(bounds.height) ++ "px")
        let bottom = dom.query_selector(rulers[i],".ruler-trailing")
        dom.style_set_property(dom.query_selector(rulers[i],"[data-ruler-margin=bottom]"),"top",string(bounds.height-dom.bounding_box(bottom).height) ++ "px")
    }
}
view <document_scrollport> { ~.rendered }
on render(evt) {
    if (evt.event_phase==2) emit("word_viewport_changed",evt)
    return 'pass'
}
on scroll(evt) { emit("word_viewport_changed",evt); return 'handled' }
pub fn viewport(rendered) => apply(<document_scrollport rendered:rendered>)

fn owner(node) => dom.get_element_by_id(dom.document_element(node),"ruler-workspace")
pub fn dragging(node) => dom.get_attribute(dom.document_element(node),"data-word-ruler-drag")!=null
fn stage(node) => dom.get_element_by_id(dom.document_element(node),"stage")
fn delta(drag,evt,node) {
    let scroll = dom.scroll_state(stage(node));
    (if (drag.vertical) evt.clientY-drag.y+scroll.y-drag.scroll.y else evt.clientX-drag.x+scroll.x-drag.scroll.x)/drag.zoom
}
fn value(editor,drag,amount) => if (drag.margin) margin_value(editor.doc,drag.handle,amount,drag.page_no)
    else indent_value(drag.geometry.values,drag.geometry.width,drag.handle,amount)
fn gesture(editor,node,handle,zoom) {
    let margin = dom.get_attribute(handle,"data-ruler-margin")
    let kind = margin or dom.get_attribute(handle,"data-ruler-indent");
    if (kind==null) null else {handle:kind,margin:margin!=null,vertical:contains(["top","bottom"],margin),
        selection:editor.selection,zoom:zoom,geometry:paragraph_geometry(editor,node,zoom),
        control:dom.get_attribute(handle,"id"),scroll:dom.scroll_state(stage(node)),
        page_no:int(dom.get_attribute(dom.closest(handle,".ruler-page,#horizontal-ruler"),"data-page")) or 1}
}
pn paint_rulers(doc,node,zoom) {
    let root = dom.document_element(node)
    let initial = page.settings(doc)
    let regions = [dom.get_element_by_id(root,"horizontal-ruler"),*dom.query_selector_all(root,"#vertical-ruler .ruler-page")]
    for (i,region in regions) {
        let vertical = i>0
        let page_no = if (vertical) i else int(dom.get_attribute(region,"data-page")) or 1
        let g = page.geometry(doc,page_no)
        let p = page.resolved(initial,page_no)
        for (s in sides where s.vertical==vertical) {
            dom.style_set_property(dom.query_selector(region,if (contains(["left","top"],s.id)) ".ruler-leading" else ".ruler-trailing"),
                if (vertical) "height" else "width",string(g[s.id]*zoom) ++ "px")
            let handle = dom.query_selector(region,"[data-ruler-margin=" ++ s.id ++ "]")
            dom.style_set_property(handle,if (vertical) "top" else "left",string((if (s.sign==1) g[s.id]*zoom
                else (if (vertical) dom.bounding_box(region).height else g.width*zoom)-g[s.id]*zoom)) ++ "px")
            attribute(handle,"aria-valuenow",string(p[s.id]))
            attribute(handle,"aria-valuetext",string(p[s.id]) ++ " mm")
            attribute(handle,"aria-label",margin_name(p,s,page_no) ++ " page margin")
            attribute(handle,"title",margin_name(p,s,page_no) ++ " page margin — drag to resize")
        }
        for (tick in dom.query_selector_all(region,".ruler-mark"))
            dom.style_set_property(tick,if (vertical) "top" else "left",string(((if (vertical) g.top else g.left)+
                float(dom.get_attribute(tick,"data-ruler-cm"))*96/2.54)*zoom) ++ "px")
    }
}
pn preview(editor,node,drag,params) {
    if (drag.margin) {
        let tx = page.transaction(editor.doc,editor.selection,{*:page.settings(editor.doc),*:params}) ^ { return }
        for (i,sheet in dom.query_selector_all(controller.surface(dom.document_element(node)),".word-page")) {
            let g = page.geometry(tx.doc_after,i+1)
            for (s in sides) dom.style_set_property(sheet,"padding-" ++ s.id,string(g[s.id]) ++ "px")
            for (story in dom.query_selector_all(sheet,".word-story"))
                for (side in ["left","right"]) dom.style_set_property(story,side,string(g[side]) ++ "px")
        }
        paint_rulers(tx.doc_after,node,drag.zoom)
    } else {
        for (e in drag.targets) {
            let n = block_node(editor,node,e.path)
            let cell = contains(["td","th"],lower(dom.node_name(n)))
            for (key,amount in params) {
                let property = {indent:"margin-left",right:"margin-right",first:"text-indent"}[string(key)]
                if (cell and key!="first") dom.style_set_property(n,if (key=="indent") "padding-left" else "padding-right",
                    string(e.padding[string(key)]+(amount-e.values[string(key)])/0.75) ++ "px")
                else dom.style_set_property(n,property,string(amount) ++ "pt")
            }
        }
    }
    sync(stage(node))
    indent_position(editor,node,drag.zoom)
    guide_position(node,drag)
    refresh(node)
}
pn guide_position(node,drag) {
    let control = dom.get_element_by_id(dom.document_element(node),drag.control)
    let region = dom.bounding_box(dom.parent_node(control))
    // inline offsets are live before the marker's next layout; its old client rect would lag a move.
    let offset = model.points(model.css_get(dom.get_attribute(control,"style"),if (drag.vertical) "top" else "left"))/0.75
    let viewport = dom.bounding_box(stage(node))
    let guide = dom.get_element_by_id(dom.document_element(node),"ruler-guide")
    let x = region.x+offset
    let y = region.y+offset
    attribute(guide,"style",if ((not drag.vertical and (x<viewport.x or x>viewport.x+viewport.width)) or
        (drag.vertical and (y<viewport.y or y>viewport.y+viewport.height))) "visibility:hidden" else
        "left:" ++ string(if (drag.vertical) viewport.x else x) ++ "px;top:" ++ string(if (drag.vertical) y else viewport.y) ++
        "px;width:" ++ string(if (drag.vertical) viewport.width else 1) ++ "px;height:" ++ string(if (drag.vertical) 1 else viewport.height) ++ "px")
}
pn end_drag(editor,node,drag,release=true) {
    for (entry in drag.styles) {
        if (entry.style==null) {
            if (dom.get_attribute(entry.node,"style")!=null) dom.remove_attribute(entry.node,"style")
        }
        else attribute(entry.node,"style",entry.style)
    }
    paint_rulers(editor.doc,node,drag.zoom)
    sync(stage(node))
    indent_position(editor,node,drag.zoom)
    attribute(dom.get_element_by_id(dom.document_element(node),"ruler-guide"),"style","visibility:hidden")
    refresh(node)
    if (release) dom.release_pointer_capture(owner(node),drag.pointer)
    dom.remove_attribute(dom.document_element(node),"data-word-ruler-drag")
}
view <word_rulers> state drag:null {
    <section id:"ruler-workspace",class:"canvas-panel",tabindex:"-1",["aria-label"]:"Document workspace",
        *:if (drag==null) {} else {["data-ruler-control"]:drag.control},
        <div id:"canvas-chrome",class:"canvas-chrome",horizontal(~.editor.doc,~.zoom,~.disabled or ~.read_only)>;
        <div class:"canvas-body",vertical(~.editor.doc,~.zoom,~.disabled or ~.read_only);viewport(~.rendered)>;
        <div id:"ruler-guide",class:"ruler-guide",style:"visibility:hidden",["aria-hidden"]:"true">>
}
on render(evt) {
    if (evt.event_phase==2) refresh(evt.target)
    return 'pass'
}
on mousedown(evt) {
    if (drag!=null or dom.closest(evt.target,".ruler-control")!=null) { return 'prevent-default' }
    return 'pass'
}
on pointerdown(evt) {
    let control = dom.closest(evt.target,".ruler-control")
    if (evt.button!=0 or ~.disabled or ~.read_only or control==null) { return 'pass' }
    let initial = gesture(~.editor,evt.target,control,~.zoom)
    if (initial==null or (not initial.margin and initial.geometry==null)) { return 'prevent-default' }
    let targets = [for (e in model.selected_blocks(~.editor)) {path:e.path,
        values:model.paragraph_indents({*:~.editor,selection:{kind:'node',path:e.path}}),
        padding:{indent:pixels(block_node(~.editor,evt.target,e.path),"padding-left"),right:pixels(block_node(~.editor,evt.target,e.path),"padding-right")}}]
    // publish the claim before focus can send selectionchange; template attributes update after this handler.
    attribute(dom.document_element(evt.target),"data-word-ruler-drag",initial.control)
    drag = {*:initial,pointer:evt.pointerId,x:evt.clientX,y:evt.clientY,targets:targets,
        styles:[for (n in dom.query_selector_all(controller.surface(dom.document_element(evt.target)),".word-page,.word-story," ++ model.block_selector))
            {node:n,style:dom.get_attribute(n,"style")}]}
    controller.focus(dom.document_element(evt.target))
    owner(evt.target).set_pointer_capture(evt.pointerId)
    dom.style_set_property(dom.get_element_by_id(dom.document_element(evt.target),"selection-toolbar"),"visibility","hidden")
    return 'prevent-default'
}
on pointermove(evt) {
    if (drag==null or drag.pointer!=evt.pointerId) { return 'pass' }
    preview(~.editor,evt.target,drag,value(~.editor,drag,delta(drag,evt,evt.target)))
    return 'prevent-default'
}
on pointerup(evt) {
    if (drag==null or drag.pointer!=evt.pointerId) { return 'pass' }
    let before = drag
    let params = value(~.editor,before,delta(before,evt,evt.target))
    let changed = params!=value(~.editor,before,0)
    drag = null
    end_drag(~.editor,evt.target,before)
    if (changed) emit("word_command",{id:if (before.margin) "layout.pageSetup" else "para.indents",params:params,
        selection:before.selection,node:owner(evt.target),event:evt})
    else { controller.focus(dom.document_element(evt.target)); controller.restore(~.editor); emit("word_ruler_end",{target:owner(evt.target)}) }
    return 'prevent-default'
}
on pointercancel(evt) {
    if (drag==null) { return 'pass' }
    let before = drag
    drag = null
    end_drag(~.editor,evt.target,before)
    controller.focus(dom.document_element(evt.target))
    controller.restore(~.editor)
    emit("word_ruler_end",{target:owner(evt.target)})
    return 'prevent-default'
}
on lostpointercapture(evt) {
    if (drag!=null) { let before = drag; drag = null; end_drag(~.editor,evt.target,before,false) }
    return 'pass'
}
on keydown(evt) {
    if (drag!=null) {
        if (evt.key=="Escape") {
            let before = drag
            drag = null
            end_drag(~.editor,evt.target,before)
            controller.focus(dom.document_element(evt.target))
            controller.restore(~.editor)
            emit("word_ruler_end",{target:owner(evt.target)})
        }
        return 'prevent-default'
    }
    let control = dom.closest(evt.target,".ruler-control")
    if (control==null or ~.read_only or ~.disabled) { return 'pass' }
    let before = gesture(~.editor,evt.target,control,~.zoom)
    let keys = if (before.vertical) ["ArrowUp","ArrowDown"] else ["ArrowLeft","ArrowRight"]
    if (not contains(keys,evt.key)) { return 'pass' }
    let amount = (if (evt.key==keys[0]) -1 else 1)*(if (before.margin) 0.5*96/25.4 else 96/72)*(if (evt.shiftKey) 10 else 1)
    emit("word_command",{id:if (before.margin) "layout.pageSetup" else "para.indents",params:value(~.editor,before,amount),
        selection:before.selection,node:owner(evt.target),event:evt,ruler_focus:before.control})
    return 'prevent-default'
}
pub fn workspace(editor,zoom,read_only,disabled,rendered) =>
    apply(<word_rulers editor:editor,zoom:zoom,read_only:read_only,disabled:disabled,rendered:rendered>)

pub pn refresh(node) {
    dom.request_frame(dom.get_element_by_id(dom.document_element(node),"word"),"word_ruler_geometry_ready")
}
fn visible_page(editor,node) {
    let viewport = dom.bounding_box(stage(node))
    let visible = [for (i,sheet in dom.query_selector_all(controller.surface(dom.document_element(node)),".word-page"),
        let box=dom.bounding_box(sheet) where box.y+box.height>viewport.y+1 and box.y<viewport.y+viewport.height) i+1]
    let caret = navigation.current_page(editor)
    if (contains(visible,caret)) caret else visible[0] or caret
}
pub pn position(editor,node,zoom) {
    sync(stage(node))
    if (not dragging(node)) {
        let ruler = dom.get_element_by_id(dom.document_element(node),"horizontal-ruler")
        let page_no = visible_page(editor,node)
        if (dom.get_attribute(ruler,"data-page")!=string(page_no)) {
            attribute(ruler,"data-page",string(page_no))
            paint_rulers(editor.doc,node,zoom)
        }
    }
    indent_position(editor,node,zoom)
    let id = dom.get_attribute(dom.document_element(node),"data-word-ruler-drag")
    if (id!=null) {
        let control = dom.get_element_by_id(dom.document_element(node),id)
        guide_position(node,{control:id,vertical:contains(["top","bottom"],dom.get_attribute(control,"data-ruler-margin"))})
    }
}
