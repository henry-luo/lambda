// S9.1.2 / S12.1.3: the crop rectangle is a view draft until explicitly applied.
import core: .core
import model: .model

pub fn rectangle(image,start,end) any^ {
    if (not all([for (n in [start.x,start.y,end.x,end.y]) model.finite(n)]))
        raise error("Raster: crop coordinates must be finite")
    // outward pixel rounding must not turn a click or line into a crop.
    else if (start.x == end.x or start.y == end.y) null
    else {
        let x = int(floor(max(0,min(image.width,min(start.x,end.x)))))
        let y = int(floor(max(0,min(image.height,min(start.y,end.y)))))
        let right = int(ceil(max(0,min(image.width,max(start.x,end.x)))))
        let bottom = int(ceil(max(0,min(image.height,max(start.y,end.y)))));
        if (right<=x or bottom<=y) null else {x:x,y:y,width:right-x,height:bottom-y}
    }
}
pub fn apply_draft(st) any^ {
    if (st.crop == null) raise error("Raster: drag to choose a crop first")
    else {*:core.exec(st,{id:"document.crop",rect:[st.crop.x,st.crop.y,st.crop.x+st.crop.width,st.crop.y+st.crop.height]})^,crop:null}
}
