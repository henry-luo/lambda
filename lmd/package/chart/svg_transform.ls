// SVG affine transforms are shared by coordinate projection and transition interpolation.
import util: .util
type operation = \(a+ s* "(" (d | s | "," | "." | "+" | "-" | "e" | "E")* ")")
type number_token = \(("+" | "-")? (d+ ("." d*)? | "." d+) (("e" | "E") ("+" | "-")? d+)?)
type separators = \((s | ",")*)
pub let identity = [1.0, 0.0, 0.0, 1.0, 0.0, 0.0]
pub fn project(matrix, point) => [matrix[0] * point[0] + matrix[2] * point[1] + matrix[4],
    matrix[1] * point[0] + matrix[3] * point[1] + matrix[5]]
pub fn multiply(a, b) => [a[0]*b[0]+a[2]*b[1], a[1]*b[0]+a[3]*b[1],
    a[0]*b[2]+a[2]*b[3], a[1]*b[2]+a[3]*b[3], a[0]*b[4]+a[2]*b[5]+a[4], a[1]*b[4]+a[3]*b[5]+a[5]]
fn rotation(angle) => [math.cos(angle), math.sin(angle), -math.sin(angle), math.cos(angle), 0.0, 0.0]
fn single(source) {
    let bracket=index_of(source,"(");
    let kind=trim(slice(source,0,bracket));
    let body=slice(source,bracket+1,len(source)-1);
    let matches=find(body,number_token);
    let values=matches |> float(~.value);
    let rest=replace(body,number_token,"");
    let angle=values[0]*util.PI/180.0;
    let result=if (kind=="matrix" and len(values)==6) values
        else if (kind=="translate" and contains([1,2],len(values))) [1.0,0.0,0.0,1.0,values[0],if (len(values)==2) values[1] else 0.0]
        else if (kind=="scale" and contains([1,2],len(values))) [values[0],0.0,0.0,if (len(values)==2) values[1] else values[0],0.0,0.0]
        else if (kind=="rotate" and contains([1,3],len(values))) if (len(values)==1) rotation(angle)
            else multiply(multiply([1.0,0.0,0.0,1.0,values[1],values[2]],rotation(angle)),[1.0,0.0,0.0,1.0,-values[1],-values[2]])
        else if (kind=="skewX" and len(values)==1) [1.0,0.0,math.tan(angle),1.0,0.0,0.0]
        else if (kind=="skewY" and len(values)==1) [1.0,math.tan(angle),0.0,1.0,0.0,0.0] else null;
    if (not (rest is separators) or result==null or not all(result |> util.finite_number(~))) error("chart: invalid SVG transform") else result
}
fn compose(steps,index=0,matrix=identity) => if (matrix is error or index>=len(steps)) matrix
    else (let next=single(steps[index]),if (next is error) next else compose(steps,index+1,multiply(matrix,next)))
pub fn matrix(source) {
    if (source==null or source=="") identity else {
        let steps=find(source,operation) |> ~.value;
        let rest=replace(source,operation,"");
        if (not (rest is separators)) error("chart: invalid SVG transform sequence") else compose(steps)
    }
}
pub fn css(matrix) => "matrix("++join(matrix |> util.fmt_num(~),",")++")"
pub fn interpolate(a,b,t) {
    let aa=matrix(a);let bb=matrix(b);
    if (aa is error) aa else if (bb is error) bb else css([for (i,value in aa) util.lerp(value,bb[i],t)])
}
