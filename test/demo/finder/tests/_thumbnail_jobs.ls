// D4.5.2: exercise copied worker input and transient result arrays under forced GC.
import dom
import radiant
let RED = resize(as_ubyte([[[255,0,0,255]]]),640,480)
let BLUE = resize(as_ubyte([[[0,0,255,255]]]),640,480)
let PATH = "temp/finder-job-source.png"
fn owner(evt) => dom.get_element_by_id(dom.root_node(evt.target),"probe")
view <thumbnail_probe> state session:{job:0,frame:0,label:""} { ~.rendered }
on click(evt) {
    let button = dom.closest(evt.target,"button")
    if (button == null) { return 'pass' }
    let root = owner(evt)
    let action = dom.get_attribute(button,"id")
    var path = PATH
    if (action == "red" or action == "blue") { save(if (action == "red") RED else BLUE,PATH)^ }
    if (action == "bad") { output("broken PNG", "temp/finder-job-bad.png",{format:'text'})^; path = "temp/finder-job-bad.png" }
    if (action == "bad-cached") { path = "temp/finder-job-bad.png" }
    if (action == "missing") { path = "temp/finder-job-does-not-exist.png" }
    if (action == "invalidate") { radiant.image_job(root,{action:"invalidate",channel:"thumbnail"})^ }
    if (action == "supersede") {
        let stale = radiant.image_job(root,{action:"start",channel:"thumbnail",path:PATH,width:80,height:60})^
        radiant.image_job(root,{action:"cancel",channel:"thumbnail"})^
        let abandoned = radiant.image_job(root,{action:"poll",channel:"thumbnail",token:stale})^
        dom.set_attribute(root,"data-stale",string(abandoned == null))
    }
    let job = radiant.image_job(root,{action:"start",channel:"thumbnail",path:path,width:80,height:60,
        canvas_bytes:if (action == "budget") 33554432 else 0})^
    session = {job:job,label:action,frame:if (session.frame>0) session.frame else dom.request_frame(root,"thumbnail_ready")}
    return 'handled'
}
on thumbnail_ready(evt) {
    if (evt.event_phase != 2 or evt.detail != session.frame) { return 'pass' }
    let done = radiant.image_job(evt.target,{action:"poll",channel:"thumbnail",token:session.job})^
    if (done == null) { session = {*:session,frame:dom.request_frame(evt.target,"thumbnail_ready")} }
    else {
        if (done.success) dom.set_canvas_pixels(dom.get_element_by_id(evt.target,"canvas"),done.pixels)^
        for (key,value in {success:done.success,cache:done.cache_hit,label:session.label,error:done.error.status,bytes:done.cache_bytes or 0})
            dom.set_attribute(evt.target,"data-" ++ string(key),string(value))
        session = {*:session,job:0,frame:0}
    }
    return 'handled'
}
on closerequest(evt) {
    if (session.frame>0) dom.cancel_frame(owner(evt),session.frame)
    radiant.image_job(owner(evt),{action:"cancel",channel:"thumbnail"})^
    return 'pass'
}
<html <head <style "body{margin:0;background:white}canvas{display:block;width:80px;height:60px}">>
    apply(<thumbnail_probe rendered:<body id:"probe",<canvas id:"canvas",width:"1",height:"1">;
        *[for (id in ["red","cached","blue","invalidate","budget","bad","bad-cached","missing","supersede"]) <button id:id,id>]>>)
>
