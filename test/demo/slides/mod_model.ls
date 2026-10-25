// S1.4 / S12.1.1v2: authored decks and undo snapshots are immutable values.
import slide: lambda.slide
import c: lambda.slide.common
import factory: lambda.ui.core.element

let tags = {
    text:(a,b) => <text *:a,*b>, shape:(a,b) => <shape *:a,*b>,
    image:(a,b) => <image *:a,*b>, content:(a,b) => <content *:a,*b>,
    group:(a,b) => <group *:a,*b>, cue:(a,b) => <cue *:a,*b>,
    effect:(a,b) => <effect *:a,*b>, parallel:(a,b) => <parallel *:a,*b>,
    sequence:(a,b) => <sequence *:a,*b>
}
pub fn rebuild(node,attrs,children) => factory.create(tags,name(node),attrs,children)
pub fn attrs(node,values) => rebuild(node,{*:map(node),*:values},content(node))
pub fn scenes(deck) => c.children(deck,'slide')
pub fn scene(st) => st.plan.slides[st.index]
pub fn chosen(st) => [for (obj in scene(st).targets where obj.dom_key == st.selected) obj][0]
pub fn text(node) => join([for (child in content(node)) if (child is element) text(child) else string(child)],"")
pub fn plain(node) => all([for (child in content(node)) child is string])
pub fn notes(st) => text(<notes *scene(st).notes>)
pub fn with_slides(deck,items) => <presentation *:map(deck),
    *[*[for (child in content(deck) where name(child) != 'slide') child], *items]>
pub fn replace_slide(deck,index,node) => with_slides(deck,[for (i,s in scenes(deck)) if (i == index) node else s])
pub fn starter() => <presentation title:"Untitled presentation",width:1280.0,height:720.0,theme:'light',
    <slide id:"opening",title:"Your next big idea",layout:'title-body',
        <shape id:"accent",x:80.0,y:76.0,width:56.0,height:8.0,fill:"#c54c31",radius:4.0>
        <text id:"title",x:80.0,y:170.0,width:1100.0,height:160.0,font_size:64.0,color:"#202d40",
            style:"font-weight:700;line-height:1.15;","Your next big idea">
        <text id:"subtitle",x:84.0,y:378.0,width:1040.0,height:100.0,font_size:28.0,color:"#64748b",
            "A little clarity. A compelling story. Make it yours.">
        <notes "Introduce your idea and why it matters.">>
    <slide id:"story",title:"Tell a compelling story",layout:'title-body',
        <text id:"title",role:'title',"Tell a compelling story">
        <text id:"body",role:'body',"Start with the opportunity.\nExplain your idea.\nGive people a next step.">>>
pub fn initial(deck=null,base="") {
    let doc = if (deck == null) starter() else deck;
    {doc:doc,plan:slide.compile(doc)^,saved:doc,index:0,selected:"",undo:[],redo:[],
        base:base,path:"",status:"Ready to create",error:"",dialog:"",tab:"Home",zoom:1.0,
        clipboard:null,drag:null,editing:null,show:false,grid:false,serial:1}
}
fn snapshot(st) => {doc:st.doc,index:st.index,selected:st.selected,base:st.base,path:st.path}
pub fn commit_doc(st,doc,index=null,selected=null) any^ {
    let plan = if (doc == st.doc) st.plan else slide.compile(doc)^;
    {*:st,doc:doc,plan:plan,index:if (index == null) st.index else index,
        selected:if (selected == null) st.selected else selected,error:"",redo:if (doc == st.doc) st.redo else [],
        undo:if (doc == st.doc) st.undo else [*slice(st.undo,max(0,len(st.undo)-79)),snapshot(st)]}
}
pub fn history(st,forward=false) {
    let stack = if (forward) st.redo else st.undo
    let entry = stack[last];
    if (entry == null) st else {*:st,*:entry,plan:slide.compile(entry.doc)^,editing:null,drag:null,error:"",
        undo:if (forward) [*st.undo,snapshot(st)] else slice(st.undo,0,len(st.undo)-1),
        redo:if (forward) slice(st.redo,0,len(st.redo)-1) else [*st.redo,snapshot(st)]}
}
pub fn select_slide(st,index) => {*:st,index:max(0,min(len(st.plan.slides)-1,index)),selected:"",error:""}
// Editing a composed slide detaches that slide from its master; other slides keep inheritance.
pub fn scene_doc(st,objects,extra={},tail=null) {
    let src = scene(st).source
    let rest = if (tail == null) [for (child in content(src) where not contains(c.objects(src),child)) child] else tail;
    replace_slide(st.doc,st.index,<slide *:map(src),master:false,*:extra,*[*objects,*rest]>)
}
fn map_objects(objects,path,target,change) => [for (i,node in objects) {
    let key = path ++ (if (contains(path,"-o")) "-" else "-o") ++ string(i)
    let next = if (key == target) change(node) else if (name(node) == 'group')
        rebuild(node,map(node),map_objects(c.objects(node),key,target,change)) else node;
    *if (next == null) [] else [next]
}]
pub fn edit_object(st,change) any^ {
    if (chosen(st) == null) st else commit_doc(st,scene_doc(st,
        map_objects(c.objects(scene(st).source),"s" ++ string(st.index),st.selected,change)))^
}
pub fn properties(st,values) any^ => edit_object(st,(node) => attrs(node,values))^
pub fn set_text(st,value,rich=false) any^ {
    let obj = chosen(st)
    let children = if (rich) content(parse("<wrapper " ++ value ++ ">",'mark')^) else [value];
    if (obj == null or not contains(['text','content'],obj.tag) or content(obj.source) == children) st
    else edit_object(st,(node) => rebuild(node,map(node),children))^
}
pub fn slide_properties(st,values) any^ => commit_doc(st,replace_slide(st.doc,st.index,
    <slide *:map(scenes(st.doc)[st.index]),*:values,*content(scenes(st.doc)[st.index])>))^
pub fn set_notes(st,value) any^ => commit_doc(st,scene_doc(st,c.objects(scene(st).source),{},
    [*[for (child in content(scene(st).source) where not contains(c.objects(scene(st).source),child) and name(child) != 'notes') child],<notes value>]))^
fn free_id(ids,prefix,n=1) => if (contains(ids,prefix ++ string(n))) free_id(ids,prefix,n+1) else prefix ++ string(n)
fn free_object_id(ids,prefix,n) {
    let id = prefix ++ string(n);
    if (any([for (used in ids) used == id or starts_with(used,id ++ "-")])) free_object_id(ids,prefix,n+1) else id
}
fn object_id(st,prefix="object-") => free_object_id([for (obj in scene(st).targets) obj.id],prefix,st.serial)
pub fn add_object(st,kind) any^ {
    let id = object_id(st)
    let node = if (kind == "text") <text id:id,x:160.0,y:220.0,width:600.0,height:110.0,font_size:36.0,"Your text here">
        else if (kind == "image") <image id:id,x:180.0,y:180.0,width:480.0,height:320.0,src:"",fit:'contain'>
        else <shape id:id,x:200.0,y:220.0,width:300.0,height:180.0,kind:symbol(kind),fill:"#c54c31",radius:if (kind == "rect") 12.0 else 0.0>
    let objects = [*c.objects(scene(st).source),node];
    {*:commit_doc(st,scene_doc(st,objects),null,"s" ++ string(st.index) ++ "-o" ++ string(len(objects)-1))^,serial:st.serial+1}
}
fn clone(node,prefix) => attrs(if (name(node) == 'group') rebuild(node,map(node),
    [for (i,child in content(node)) clone(child,prefix ++ "-" ++ string(i))]) else node,{id:prefix})
pub fn paste(st,source=null) any^ {
    let node = if (source == null) st.clipboard else source
    let id = object_id(st,"copy-")
    let objects = c.objects(scene(st).source);
    if (node == null) st else {*:commit_doc(st,scene_doc(st,[*objects,
        attrs(clone(node,id),{x:c.value(node.x,0.0)+24.0,y:c.value(node.y,0.0)+24.0})]),null,
        "s" ++ string(st.index) ++ "-o" ++ string(len(objects)))^,serial:st.serial+1}
}
// Drop effects for deleted descendants, including empty parallel/sequence/cue wrappers.
fn prune(node,ids) {
    if (name(node) == 'effect') { if (contains(ids,string(node.target))) null else node }
    else if (contains(['cue','parallel','sequence'],name(node))) {
        let children = [for (child in content(node)) (let next = prune(child,ids), *if (next == null) [] else [next])];
        if (len(children) == 0) null else rebuild(node,map(node),children)
    } else node
}
pub fn remove_object(st) any^ {
    let obj = chosen(st)
    let ids = [for (target in scene(st).targets where target.dom_key == st.selected or starts_with(target.dom_key,st.selected ++ "-")) target.id]
    let rest = [for (child in content(scene(st).source) where not contains(c.objects(scene(st).source),child))
        (let next = prune(child,ids), *if (next == null) [] else [next])];
    if (obj == null) st else commit_doc(st,scene_doc(st,map_objects(c.objects(scene(st).source),"s" ++ string(st.index),st.selected,(n) => null),{},rest),null,"")^
}
pub fn add_slide(st,duplicate=false) any^ {
    let slides = scenes(st.doc)
    let id = free_id([for (s in st.plan.slides) s.id],"slide-")
    let src = slides[st.index]
    let node = if (duplicate) <slide *:map(src),id:id,title:c.value(src.title,"Slide") ++ " (copy)",*content(src)>
        else <slide id:id,title:"New slide",layout:'title-body',
            <text id:"title",role:'title',"Slide title"><text id:"body",role:'body',"Add your ideas here">>;
    commit_doc(st,with_slides(st.doc,[*slice(slides,0,st.index+1),node,*slice(slides,st.index+1)]),st.index+1,"")^
}
pub fn delete_slide(st) any^ {
    let slides = scenes(st.doc);
    if (len(slides) == 1) {*:st,status:"Keep at least one slide in your presentation."}
    else commit_doc(st,with_slides(st.doc,[for (i,s in slides where i != st.index) s]),min(st.index,len(slides)-2),"")^
}
pub fn move_slide(st,delta) any^ {
    let slides = scenes(st.doc)
    let next = max(0,min(len(slides)-1,st.index+delta));
    commit_doc(st,with_slides(st.doc,[for (i,s in slides) if (i == next) slides[st.index] else if (i == st.index) slides[next] else s]),next,"")^
}
pub fn parent(st) => [for (obj in scene(st).targets where obj.tag == 'group' and
    starts_with(st.selected,obj.dom_key ++ "-")) obj][last]
pub fn reorder(st,front=true) any^ {
    let obj = chosen(st)
    let owner = parent(st)
    let objects = c.objects(if (owner == null) scene(st).source else owner.source)
    let prefix = if (owner == null) "s" ++ string(st.index) ++ "-o" else owner.dom_key ++ "-"
    let position = int(slice(st.selected,len(prefix)))
    let rest = [for (i,node in objects where i != position) node];
    let ordered = if (front) [*rest,obj.source] else [obj.source,*rest]
    let changed = if (owner == null) ordered else map_objects(c.objects(scene(st).source),"s" ++ string(st.index),owner.dom_key,
        (node) => rebuild(node,map(node),ordered));
    if (obj == null or position == (if (front) len(rest) else 0)) st else commit_doc(st,scene_doc(st,changed),null,
        prefix ++ string(if (front) len(rest) else 0))^
}
pub fn animate(st,kind,start="click") any^ {
    let obj = chosen(st)
    let id = if (obj.source.id == null) object_id(st,"animated-") else string(obj.source.id)
    let prepared = if (obj.source.id == null) properties(st,{id:id})^ else st
    let src = scenes(prepared.doc)[prepared.index]
    let cues = c.children(src,'cue')
    let cue = <cue start:symbol(start),<effect target:id,kind:symbol(kind),duration:450.0>>
    let children = [*[for (child in content(src) where name(child) != 'cue') child],
        *if (start == "entry") [cue,*cues] else [*cues,cue]];
    if (obj == null) st else commit_doc(st,replace_slide(prepared.doc,st.index,<slide *:map(src),*children>))^
}
pub fn clear_animations(st) any^ {
    let src = scenes(st.doc)[st.index];
    commit_doc(st,replace_slide(st.doc,st.index,<slide *:map(src),*[for (child in content(src) where name(child) != 'cue') child]>))^
}
pub fn open_deck(st,deck,base,path="") any^ {
    let next = commit_doc(st,deck,0,"")^;
    {*:next,base:c.value(deck.base_uri,base),path:path,saved:deck,dialog:"",status:"Opened " ++ c.value(deck.title,"presentation")}
}
