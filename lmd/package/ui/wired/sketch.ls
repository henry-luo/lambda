import c: lambda.ui.core.component

// seeded, bounded offsets keep pure presentation stable across application updates (S12.1.3).
fn jitter(seed, index) => float(((seed + 1) * (index * 73 + 19) * 48271) % 997) / 997.0 - 0.5
fn point(x, y) => string(x) ++ " " ++ string(y)
fn stroke(seed, pass, edge) {
    let corners = [[2,2],[98,2],[98,98],[2,98]]
    let a = corners[edge]
    let b = corners[(edge+1)%4]
    let index = pass*17+edge*4
    let dx = jitter(seed,index)*1.4
    let dy = jitter(seed,index+1)*1.4;
    "M" ++ point(a[0]+dx,a[1]+dy) ++ " Q" ++
        point((a[0]+b[0])/2+jitter(seed,index+2)*2,(a[1]+b[1])/2+jitter(seed,index+3)*2) ++
        " " ++ point(b[0]-dx,b[1]-dy)
}
pub fn outline(node, line = false) {
    let p = c.props(node)
    let seed = c.option(p,"seed",len(c.text(p.id)) + len(c.text(p.title)) + len(content(node)))
    let elevation = c.option(p,"elevation",1);
    <svg class:"wired-sketch",viewBox:"0 0 100 100",preserveAspectRatio:"none",width:"100%",height:"100%",
        ['aria-hidden']:"true",focusable:"false",
        <g fill:"none",stroke:"currentColor",['stroke-linecap']:"round",['stroke-linejoin']:"round",
            *[for (pass in 0 to 1, edge in (if (line) [0] else [0,1,2,3]))
                <path d:stroke(seed,pass,edge),['vector-effect']:"non-scaling-stroke">,
            for (level in 2 to elevation where elevation > 1)
                <path class:"wired-elevation",d:"M" ++ point(level,98-level) ++ " Q50 " ++ string(100-level/2) ++ " " ++ point(99-level/2,99-level/2) ++ " L" ++ point(99-level/2,level),
                    opacity:0.65-float(level)*0.08,['vector-effect']:"non-scaling-stroke">]>>
}
