// Page-flip geometry for the reader. A leaf folds along the line that bisects
// its corner's place of rest and where the corner is now (the soft-page fold
// of StPageFlip): the page stays flat on the spine side of the fold, and the
// lifted part shows its back, reflected over the fold. Everything is worked out
// for a forward turn of the right-hand page, then mirrored for a backward one.
//
// The turn layer holds full-size copies of the stage: the leaf (the old page),
// the held facing page, the back (the new facing page, or blank paper on one
// page) and a cast shadow. Each takes a transform and a clip polygon in its own
// coordinates, so one fold places them all.
import dom

fn sub(a,b) => {x:a.x-b.x,y:a.y-b.y}
fn dot(a,b) => a.x*b.x + a.y*b.y
fn side(p,line) => dot(sub(p,line.m),line.n)
fn rect(x0,x1,h) => [{x:x0,y:0.0},{x:x1,y:0.0},{x:x1,y:h},{x:x0,y:h}]

// The part of a convex polygon where side(p) <= 0 (Sutherland–Hodgman, one edge).
fn edge(a,b,line) => (let fa = side(a,line), let fb = side(b,line),
    (if (fa <= 0) [a] else []) ++ (if ((fa <= 0) != (fb <= 0)) (let t = fa/(fa-fb), [{x:a.x+(b.x-a.x)*t,y:a.y+(b.y-a.y)*t}]) else []))
fn cut(poly,line) => (let k = len(poly),
    [for (kept in [for (i in 0 to k-1) edge(poly[i],poly[(i+1)%k],line)], q in kept) q])

// affine maps as CSS matrix(a,b,c,d,e,f): x' = a x + c y + e, y' = b x + d y + f
fn mul(m,n) => {a:m.a*n.a+m.c*n.b,b:m.b*n.a+m.d*n.b,c:m.a*n.c+m.c*n.d,d:m.b*n.c+m.d*n.d,
    e:m.a*n.e+m.c*n.f+m.e,f:m.b*n.e+m.d*n.f+m.f}
// the reflection over a line through m with unit normal n
fn reflect(line) => (let n = line.n, let k = 2.0*dot(line.m,n),
    {a:1.0-2.0*n.x*n.x,b:-2.0*n.x*n.y,c:-2.0*n.x*n.y,d:1.0-2.0*n.y*n.y,e:k*n.x,f:k*n.y})
// a strip's frame: x runs from the fold along n, y along the fold, centered on m
let strip_half = 2000.0
fn strip(m,n) => {a:n.x,b:n.y,c:-n.y,d:n.x,e:m.x+n.y*strip_half,f:m.y-n.x*strip_half}

fn num(x) => string(round(x*100.0)/100.0)
fn css_matrix(m) => "matrix(" ++ join([for (k in ["a","b","c","d","e","f"]) num(m[k])],",") ++ ")"
fn css_polygon(poly) => if (len(poly) < 3) "polygon(0px 0px,0px 0px,0px 0px)"
    else "polygon(" ++ join([for (p in poly) num(p.x) ++ "px " ++ num(p.y) ++ "px"],",") ++ ")"

// One page is the whole stage with its spine on the left edge; a spread's
// right-hand page has the spine in the middle.
pub fn frame(size,layout) => (let w = if (layout == "spread") size.w/2.0 else size.w,
    {w:w,h:size.h,spine:if (layout == "spread") w else 0.0,stage:size.w,spread:layout == "spread"})

// where the turning corner rests, and where it lands on the facing side
pub fn rest(f) => {x:f.spine + f.w,y:f.h}
pub fn landing(f) => {x:f.spine - f.w,y:f.h}
// The corner's way from one point to another, lifted a little on the way in
// proportion to the distance it covers: a whole turn lifts it a tenth of a page.
pub fn path(f,from,to,u) => {x:from.x + (to.x-from.x)*u,
    y:from.y + (to.y-from.y)*u - 0.05*abs(to.x-from.x)*math.sin(math.pi*u)}

// The page is paper: its corner stays within a page width of the spine's foot
// and within a diagonal of its head.
pub fn clamp(f,p) => (let foot = {x:f.spine,y:f.h}, let d = sub(p,foot), let r = math.hypot(d.x,d.y),
    let q = if (r > f.w) {x:foot.x+d.x*f.w/r,y:foot.y+d.y*f.w/r} else p,
    let diag = math.hypot(f.w,f.h), let e = sub(q,{x:f.spine,y:0.0}), let s = math.hypot(e.x,e.y),
    if (s > diag) {x:f.spine+e.x*diag/s,y:e.y*diag/s} else q)

// how far the corner has travelled, 0 at rest to 1 on the facing side
pub fn progress(f,p) => max(0.0,min(1.0,(f.spine + f.w - p.x)/(2.0*f.w)))

// The styles of a fold with the corner at p (forward-turn coordinates).
fn fold(f,p) {
    let rest = {x:f.spine + f.w,y:f.h}
    let d = sub(rest,p)
    let l = math.hypot(d.x,d.y)
    // at rest the fold sits on the page's outer edge
    let line = if (l < 0.01) {m:rest,n:{x:1.0,y:0.0}} else {m:{x:(rest.x+p.x)/2.0,y:(rest.y+p.y)/2.0},n:{x:d.x/l,y:d.y/l}}
    let page = rect(f.spine,f.spine+f.w,f.h)
    // the back lands on the facing page; on one page it is the stage's own sheet moved left of the spine
    let shift = if (f.spread) 0.0 else f.w
    let spine = {a:-1.0,b:0.0,c:0.0,d:1.0,e:2.0*f.spine,f:0.0}
    let back = mul(reflect(line),mul(spine,{a:1.0,b:0.0,c:0.0,d:1.0,e:-shift,f:0.0}))
    // the fold in the back's own coordinates; its visible part is beyond the fold
    let local = {m:{x:2.0*f.spine - line.m.x + shift,y:line.m.y},n:{x:-line.n.x,y:line.n.y}}
    let away = {m:local.m,n:{x:-local.n.x,y:-local.n.y}}
    let lifted = {m:line.m,n:{x:-line.n.x,y:-line.n.y}}
    let travel = progress(f,p)
    {leaf:cut(page,line),back:back,back_clip:cut(rect(f.spine-f.w+shift,f.spine+shift,f.h),away),
        back_shade:strip(local.m,local.n),cast:cut(page,lifted),cast_shade:strip(line.m,line.n),
        shade:f.w*(0.08 + 0.22*math.sin(math.pi*travel)),strength:math.sin(math.pi*min(1.0,travel*1.15))}
}

// A backward turn is the forward one seen in a mirror, and a top corner the
// bottom one turned upside down; `way` names the direction and the corner.
// The mirror is its own inverse, so it also takes stage points to fold coordinates.
fn mirror(f,way) => mul({a:if (way.dir == "prev") -1.0 else 1.0,b:0.0,c:0.0,d:1.0,e:if (way.dir == "prev") f.stage else 0.0,f:0.0},
    {a:1.0,b:0.0,c:0.0,d:if (way.corner == "top") -1.0 else 1.0,e:0.0,f:if (way.corner == "top") f.h else 0.0})
fn move(m,p) => {x:m.a*p.x+m.c*p.y+m.e,y:m.b*p.x+m.d*p.y+m.f}
pub fn local(f,way,p) => move(mirror(f,way),p)
fn mirrored(m,s) => {*:s,leaf:[for (q in s.leaf) move(m,q)],back:mul(m,mul(s.back,m)),back_clip:[for (q in s.back_clip) move(m,q)],
    back_shade:mul(m,s.back_shade),cast:[for (q in s.cast) move(m,q)],cast_shade:mul(m,s.cast_shade)}

pn style(node,css) { if (node != null) dom.set_attribute(node,"style",css) }

// Places the leaf, its back and their shadows for a fold with the corner at p
// (fold coordinates, see local).
pub pn paint(host,f,way,p) {
    let s = mirrored(mirror(f,way),fold(f,p))
    let width = "width:" ++ num(s.shade) ++ "px;"
    style(dom.query_selector(host,".ebook-turn-page"),"clip-path:" ++ css_polygon(s.leaf))
    style(dom.query_selector(host,".ebook-turn-back"),"transform:" ++ css_matrix(s.back) ++ ";clip-path:" ++ css_polygon(s.back_clip))
    style(dom.query_selector(host,".ebook-turn-back .ebook-flip-shade"),width ++ "transform:" ++ css_matrix(s.back_shade))
    style(dom.query_selector(host,".ebook-flip-cast"),"clip-path:" ++ css_polygon(s.cast) ++ ";opacity:" ++ num(s.strength))
    style(dom.query_selector(host,".ebook-flip-cast .ebook-flip-shade"),width ++ "transform:" ++ css_matrix(s.cast_shade))
}

// Clears what paint placed, back to the plain turn layer.
pub pn reset(host) {
    for (selector in [".ebook-turn-page",".ebook-turn-back",".ebook-turn-back .ebook-flip-shade",".ebook-flip-cast",
            ".ebook-flip-cast .ebook-flip-shade"]) {
        let node = dom.query_selector(host,selector)
        if (node != null) dom.remove_attribute(node,"style")
    }
}
