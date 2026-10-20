// S9.1.2 / S9.2.2: retouch reads frozen sources; every completed stroke publishes once.
import img: lambda.image
import model: .model
import paint: .paint
pub let kinds = ["clone","heal","dodge","burn","sponge","blur","sharpen"]
pub fn run_command(st,command) any^ {
    if (command.id == "retouch.configure") {
        let settings = {*:st.retouch,*:if (command.aligned == null) {} else {aligned:command.aligned},
            strength:model.value(command.strength,st.retouch.strength),radius:model.value(command.radius,st.retouch.radius)};
        if (not (settings.aligned is bool) or not model.finite(settings.strength) or settings.strength<0 or settings.strength>1 or
            not (settings.radius is int) or settings.radius<1 or settings.radius>32) raise error("Raster: invalid retouch settings")
        else {*:st,retouch:settings}
    }
    else if (command.id == "retouch.source") {
        let p = command.point;
        if (not (p is array) or len(p)!=2 or not all([for (v in p) model.finite(v)]) or p[0]<0 or p[1]<0 or p[0]>=st.doc.width or p[1]>=st.doc.height)
            raise error("Raster: choose a clone/heal source inside the canvas")
        else {*:st,retouch:{*:st.retouch,source:{raster:img.render_frame(st.doc)^,point:[int(floor(p[0])),int(floor(p[1]))]},anchor:null}}
    }
    else if (command.id == "retouch.stroke") {
        let kind = model.value(command.kind,st.tool)
        let options = model.value(command.options,{})
        let target = paint.extended(st,model.pixel_target(st)^,command,options)^
        let sampled = kind in ["clone","heal"]
        let source = st.retouch.source
        if (not (kind in kinds) or st.target != "pixels") raise error("Raster: retouch requires an RGBA pixel target")
        else if (sampled and source == null) raise error("Raster: Alt-click to choose a clone/heal source")
        else {
            let start = command.points[0]
            let offset = if (st.retouch.aligned and st.retouch.anchor != null) st.retouch.anchor
                else if (sampled) [source.point[0]-int(floor(start[0])),source.point[1]-int(floor(start[1]))] else [0,0]
            let native = {*:st.brush,strength:st.retouch.strength,radius:st.retouch.radius,*:options,kind:kind,
                points:[for (point in command.points) paint.local(point,target.position)],
                selection:st.selection,origin:target.position,
                *:if (sampled) {sample:source.raster,sample_offset:[offset[0]+target.position[0],offset[1]+target.position[1]]} else {},
                *:if (kind == "heal") {context:img.render_frame(st.doc)^,context_offset:target.position} else {}}
            let pixels = img.retouch(target.raster,native)^
            let changed = model.replace_pixels(st,target,pixels)^;
            {*:changed,retouch:{*:st.retouch,anchor:if (sampled and st.retouch.aligned) offset else st.retouch.anchor}}
        }
    }
    else raise error("Raster: unknown retouch command")
}
