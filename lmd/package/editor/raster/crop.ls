// S9.1.2 / S12.1.3: the crop rectangle is a view draft until explicitly applied.
import core: .core
import model: .model
import canvas: .canvas

fn fitted_size(width,height,max_width,max_height,ratio) {
    let w = int(floor(min([width,height*ratio,max_width,max_height*ratio])))
    let h = int(round(w/ratio));
    if (w<1 or h<1) null else {width:w,height:h}
}
pub fn rectangle(image,start,end,aspect="free",custom=[1,1]) any^ {
    let ratio = canvas.crop_ratio(image,aspect,custom)^
    if (not all([for (n in [start.x,start.y,end.x,end.y]) model.finite(n)]))
        raise error("Raster: crop coordinates must be finite")
    // outward pixel rounding must not turn a click or line into a crop.
    else if (start.x == end.x or start.y == end.y) null
    else if (ratio != null) {
        // clamp before constraining the ratio, then round to integral canvas pixels.
        let x = int(round(max(0,min(image.width,start.x))))
        let y = int(round(max(0,min(image.height,start.y))))
        let rightward = end.x>start.x
        let downward = end.y>start.y
        let available_x = if (rightward) image.width-x else x
        let available_y = if (downward) image.height-y else y
        let size = fitted_size(abs(max(0,min(image.width,end.x))-x),
            abs(max(0,min(image.height,end.y))-y),available_x,available_y,ratio);
        if (size == null) null else
            {x:if (rightward) x else x-size.width,y:if (downward) y else y-size.height,*:size}
    }
    else {
        let x = int(floor(max(0,min(image.width,min(start.x,end.x)))))
        let y = int(floor(max(0,min(image.height,min(start.y,end.y)))))
        let right = int(ceil(max(0,min(image.width,max(start.x,end.x)))))
        let bottom = int(ceil(max(0,min(image.height,max(start.y,end.y)))));
        if (right<=x or bottom<=y) null else {x:x,y:y,width:right-x,height:bottom-y}
    }
}
fn resized_axis(position,length,extent,at,delta) => {
    start:if (at == 0) max(0,min(position+length-1,position+delta)) else position,
    end:if (at == 1) max(position+1,min(extent,position+length+delta)) else position+length}
pub fn edit_rectangle(image,before,start,end,handle,aspect="free",custom=[1,1]) any^ {
    let ratio = canvas.crop_ratio(image,aspect,custom)^
    let at = [for (h in canvas.handles where h.id == handle) h][0]
    if (before == null or not all([for (n in [start.x,start.y,end.x,end.y]) model.finite(n)]) or
        not all([for (n in [before.x,before.y,before.width,before.height]) n is int]) or
        before.x<0 or before.y<0 or before.width<1 or before.height<1 or
        before.x+before.width>image.width or before.y+before.height>image.height or (handle != "move" and at == null))
        raise error("Raster: invalid crop edit")
    else {
        // deltas always refer to the original handle, avoiding pointer-offset jumps and accumulated rounding.
        let dx = int(round(end.x-start.x))
        let dy = int(round(end.y-start.y))
        if (handle == "move") {*:before,x:before.x+max(-before.x,min(image.width-before.x-before.width,dx)),
            y:before.y+max(-before.y,min(image.height-before.y-before.height,dy))}
        else if (ratio == null) {
            let x = resized_axis(before.x,before.width,image.width,at.x,dx)
            let y = resized_axis(before.y,before.height,image.height,at.y,dy);
            {x:x.start,y:y.start,width:x.end-x.start,height:y.end-y.start}
        }
        else {
            let cx = before.x+before.width/2
            let cy = before.y+before.height/2
            let ax = before.x+(1-at.x)*before.width
            let ay = before.y+(1-at.y)*before.height
            let horizontal = at.x != 0.5
            let vertical = at.y != 0.5
            let w = before.width+dx*(if (at.x == 0) -1 else 1)
            let h = before.height+dy*(if (at.y == 0) -1 else 1)
            let requested = if (not horizontal) h*ratio else if (not vertical) w else min(w,h*ratio)
            let maximum_w = if (not horizontal) 2*min(cx,image.width-cx) else if (at.x == 0) ax else image.width-ax
            let maximum_h = if (not vertical) 2*min(cy,image.height-cy) else if (at.y == 0) ay else image.height-ay
            let width = max(max(1,ceil(ratio/2)),requested)
            let size = fitted_size(width,width/ratio,maximum_w,maximum_h,ratio);
            if (size == null) before else {
                x:if (not horizontal) int(round(cx-size.width/2)) else if (at.x == 0) ax-size.width else ax,
                y:if (not vertical) int(round(cy-size.height/2)) else if (at.y == 0) ay-size.height else ay,*:size}
        }
    }
}
pub fn apply_draft(st) any^ {
    if (st.crop == null) raise error("Raster: drag to choose a crop first")
    else {*:core.exec(st,{id:"document.crop",rect:[st.crop.x,st.crop.y,st.crop.x+st.crop.width,st.crop.y+st.crop.height]})^,crop:null}
}
