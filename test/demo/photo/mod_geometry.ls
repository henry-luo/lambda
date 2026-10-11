// Crop coordinates are independent of preview size and layout (S1.6).
pub fn clamp(value, low, high) => min(high, max(low, value))
pub fn full() => {left:0.0, top:0.0, right:1.0, bottom:1.0}
pub fn valid(rect) => rect is map and all([for (key in ["left","top","right","bottom"])
    rect[key] is number and rect[key] >= 0 and rect[key] <= 1]) and
    rect.right > rect.left and rect.bottom > rect.top
pub fn bounds(rect, width, height) {
    let left = int(floor(rect.left * width))
    let top = int(floor(rect.top * height));
    {left:left, top:top, right:max(left+1,int(ceil(rect.right*width))),
        bottom:max(top+1,int(ceil(rect.bottom*height)))}
}
pub fn aspect(width, height, ratio) {
    let w = min(1.0, height*ratio/width)
    let h = min(1.0, width/(height*ratio));
    {left:(1-w)/2, right:(1+w)/2, top:(1-h)/2, bottom:(1+h)/2}
}
pub fn move(rect, dx, dy) {
    let x = clamp(dx,-rect.left,1-rect.right)
    let y = clamp(dy,-rect.top,1-rect.bottom);
    {left:rect.left+x, right:rect.right+x, top:rect.top+y, bottom:rect.bottom+y}
}
pub fn drag(rect, handle, dx, dy, ratio, width, height) {
    if (handle == "move") move(rect,dx,dy)
    else (
        let left = if (contains(handle,"w")) clamp(rect.left+dx,0,rect.right-1/width) else rect.left,
        let right = if (contains(handle,"e")) clamp(rect.right+dx,rect.left+1/width,1) else rect.right,
        let top = if (contains(handle,"n")) clamp(rect.top+dy,0,rect.bottom-1/height) else rect.top,
        let bottom = if (contains(handle,"s")) clamp(rect.bottom+dy,rect.top+1/height,1) else rect.bottom,
        let requested = {left:left,right:right,top:top,bottom:bottom},
        if (ratio <= 0) requested else (
            let horizontal = contains(handle,"w") or contains(handle,"e"),
            let vertical = contains(handle,"n") or contains(handle,"s"),
            let anchor_x = if (contains(handle,"w")) rect.right else if (contains(handle,"e")) rect.left else (rect.left+rect.right)/2,
            let anchor_y = if (contains(handle,"n")) rect.bottom else if (contains(handle,"s")) rect.top else (rect.top+rect.bottom)/2,
            // edge drags resize the other axis around its center; corners keep the opposite corner.
            let room_x = if (contains(handle,"w")) anchor_x else if (contains(handle,"e")) 1-anchor_x else 2*min(anchor_x,1-anchor_x),
            let room_y = if (contains(handle,"n")) anchor_y else if (contains(handle,"s")) 1-anchor_y else 2*min(anchor_y,1-anchor_y),
            let wanted = if (horizontal and (not vertical or abs(dx)*width >= abs(dy)*height*ratio))
                right-left else (bottom-top)*height*ratio/width,
            let w = min(wanted,min(room_x,room_y*height*ratio/width)),
            let h = w*width/(height*ratio),
            let x = if (contains(handle,"w")) anchor_x-w else if (contains(handle,"e")) anchor_x else anchor_x-w/2,
            let y = if (contains(handle,"n")) anchor_y-h else if (contains(handle,"s")) anchor_y else anchor_y-h/2,
            {left:x,right:x+w,top:y,bottom:y+h}
        )
    )
}
pub fn fit(width, height, available_width, available_height) => min(available_width/width,available_height/height)
pub fn inverse(x, y, origin_x, origin_y, scale) => {x:(x-origin_x)/scale,y:(y-origin_y)/scale}
