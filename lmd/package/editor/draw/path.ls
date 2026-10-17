// share the existing line/cubic motion geometry rather than introducing a second sampler.
import motion: lambda.slide.motion
pub fn points(segments) {
    let compiled = motion.compile(segments,"draw.path") ^ { null };
    [for (p in compiled.rows) {x:p.x,y:p.y}]
}
pub fn svg(segments) {
    let first = segments[0];
    "M " ++ string(first[0]) ++ " " ++ string(first[1]) ++ join([for (s in segments)
        if (len(s)==4) " L " ++ string(s[2]) ++ " " ++ string(s[3])
        else " C " ++ join([for (i in 2 to 7) string(s[i])]," ")],"")
}
// endpoint-relative controls keep custom paths attached after vertex edits.
pub fn attached(segments,from,to) {
    let a = segments[0]
    let b = segments[last]
    let dx = from.x-a[0]
    let dy = from.y-a[1]
    let ex = to.x-b[len(b)-2]
    let ey = to.y-b[len(b)-1];
    [for (i,s in segments) [for (j,n in s)
        if (i==0 and j<2) n+(if (j%2==0) dx else dy)
        else if (i==len(segments)-1 and j>=len(s)-2) n+(if (j%2==0) ex else ey)
        else if (len(s)==8 and i==0 and j<4) n+(if (j%2==0) dx else dy)
        else if (len(s)==8 and i==len(segments)-1 and j>=4) n+(if (j%2==0) ex else ey)
        else n]]
}
pub fn reversed(segments) => [for (s in reverse(segments))
    if (len(s)==4) [s[2],s[3],s[0],s[1]] else [s[6],s[7],s[4],s[5],s[2],s[3],s[0],s[1]]]
