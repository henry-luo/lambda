// Source policies supply offsets; TeX and MathML share the resulting geometry (D7.2.8).
import bx: .svg_box

pub fn fraction(numer, denom, width, num_x, up, down, axis, thickness) =>
    bx.compose([{box:numer,x:num_x,y:0.0-up},
        {box:denom,x:(width-denom.width)/2.0,y:down},
        *if (thickness>0.0) [{box:bx.rule(width,thickness,0.0-axis-thickness/2.0),x:0.0,y:0.0}] else []],width)

pub fn scripts(base, sub, sup, x_sub, x_sup, up, down, width) =>
    bx.compose([{box:base,x:0.0,y:0.0},
        *if (sup!=null) [{box:sup,x:x_sup,y:0.0-up}] else [],
        *if (sub!=null) [{box:sub,x:x_sub,y:down}] else []],width,base.type)

pub fn radical(body, sign, degree, top, thickness, offset, before, degree_y, extra) {
    let result=bx.compose([{box:sign,x:offset,y:top+sign.height},
        {box:body,x:offset+sign.width,y:0.0},
        {box:bx.rule(body.width,thickness,top),x:offset+sign.width,y:0.0},
        *if (degree!=null) [{box:degree,x:before,y:degree_y}] else []],offset+sign.width+body.width);
    {*:result,height:result.height+extra}
}
