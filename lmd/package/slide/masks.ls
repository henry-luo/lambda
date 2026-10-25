import c: .common
import paths: .transitions

fn cell(x,y,w,h,p,direction) {
    let horizontal = direction in ['left','right']
    let reverse = direction in ['right','bottom'];
    paths.rect_path(x+(if (horizontal and reverse) w*(1.0-p) else 0.0),
        y+(if (not horizontal and reverse) h*(1.0-p) else 0.0),
        w*(if (horizontal) p else 1.0),h*(if (horizontal) 1.0 else p))
}
// reuse the transition path builder; cell order is fixed for reproducible samples.
pub fn reveal(kind,progress,width,height,direction) {
    let p = max(0.0,min(1.0,progress))
    let horizontal = direction in ['left','right'];
    if (p >= 1.0) "none"
    else paths.path(if (kind == 'blinds-in') [for (i in 0 to 7)
        if (horizontal) cell(width*i/8.0,0.0,width/8.0,height,p,direction)
        else cell(0.0,height*i/8.0,width,height/8.0,p,direction)]
    else [for (row in 0 to 7) for (col in 0 to 7) {
        let order = if (kind == 'checkerboard-in') float((row+col)%2)/2.0 else float(((row*8+col)*37)%64)/64.0
        let amount = if (kind == 'dissolve-in') (if (p > order) 1.0 else 0.0) else max(0.0,min(1.0,p*2.0-order*2.0));
        cell(width*col/8.0,height*row/8.0,width/8.0,height/8.0,amount,direction)
    }])
}
