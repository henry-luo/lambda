import dom
import radiant
let RED=resize(as_ubyte([[[255,0,0,255]]]),1024,1024)
let BLUE=resize(as_ubyte([[[0,0,255,255]]]),1024,1024)
view <jobs_probe> state session:{job:0,frame:0,color:"",channel:"preview"} { ~.rendered }
on click(evt) {
    let button=dom.closest(evt.target,"button")
    if (button==null) { return 'pass' }
    let owner=dom.get_element_by_id(dom.root_node(evt.target),"fixture")
    let color=dom.get_attribute(button,"id")
    let channel=if (color=="existing") "export" else "preview"
    if (channel=="export") { save(BLUE,"temp/image-jobs-existing.png")^ }
    let token=radiant.image_job(owner,{action:"start",channel:channel,source:color,
        pixels:if (color=="red") RED else BLUE,options:{softness:6.0},
        destination:if (channel=="export") "temp/image-jobs-existing.png" else null})^
    session={*:session,job:token,color:color,channel:channel,frame:if (session.frame>0) session.frame else dom.request_frame(owner,"image_ready")}
    return 'handled'
}
on image_ready(evt) {
    if (evt.event_phase!=2 or evt.detail!=session.frame) { return 'pass' }
    let done=radiant.image_job(evt.target,{action:"poll",channel:session.channel,token:session.job})^
    if (done==null) {
        session={*:session,frame:dom.request_frame(evt.target,"image_ready")}
    } else {
        if (session.channel=="export") {
            let preserved=load("temp/image-jobs-existing.png")^
            dom.set_attribute(evt.target,"data-preserved",string(not done.success and preserved==BLUE))
            io.delete("temp/image-jobs-existing.png")^
        } else {
            dom.set_canvas_pixels(dom.get_element_by_id(evt.target,"canvas"),done.pixels^)^
            dom.set_attribute(evt.target,"data-color",session.color)
        }
        session={*:session,job:0,frame:0}
    }
    return 'handled'
}
on closerequest(evt) {
    let owner=dom.get_element_by_id(dom.root_node(evt.target),"fixture")
    if (session.frame>0) dom.cancel_frame(owner,session.frame)
    radiant.image_job(owner,{action:"cancel",channel:"preview"})
    radiant.image_job(owner,{action:"cancel",channel:"export"})
    return 'pass'
}
<html <head <style "body{margin:0;background:white}canvas{display:block;width:200px;height:200px}">>
    apply(<jobs_probe rendered:<body id:"fixture",<canvas id:"canvas",width:1,height:1>
        <button id:"red","Red"> <button id:"blue","Blue"> <button id:"existing","Existing file">>>)
>
