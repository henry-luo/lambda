// Render editable equation parameters without application selection or layout policy.
import mathdoc: lambda.doc.math.math
import colors: lambda.ui.core.component

pub fn render(p,max_width,max_height,owner="Equation") map^ {
    if (not (p.source is string) or trim(p.source)=="" or len(p.source)>4000 or
        not (p.size is number) or p.size<10 or p.size>72 or not colors.hex_color(p.color) or not (p.display is bool) or
        not contains(["left","center","right"],p.justify) or not (p.linear is bool) or
        not (p.number is string) or len(p.number)>32 or contains(p.number,"\n"))
        raise error(owner ++ ": enter a LaTeX equation (up to 4000 characters), size 10–72 px, and a #RRGGBB color")
    else {
        let ast = parse(p.source,{type:"math",flavor:"latex"})^
        let unknown = mathdoc.unmapped_commands(ast)
        if (len(unknown)>0) raise error(owner ++ ": unsupported equation command: " ++ join(unknown,", ")) else {
            let options = field_options(p)
            let measured = mathdoc.render_box(if (p.linear) <math <text_group p.source>> else ast,options)^
            let label = if (p.number=="") null else mathdoc.render_box(<math <text_group p.number>>,options)^
            let body_width = measured.width*p.size
            let body_height = (measured.height+measured.depth)*p.size
            let label_width = if (label==null) 0 else label.width*p.size
            let label_height = if (label==null) 0 else (label.height+label.depth)*p.size
            let width = max(1,ceil(body_width+(if (label==null) 0 else p.size+label_width)))
            let height = max(1,ceil(max(body_height,label_height)))
            if (width>max_width or height>max_height) raise error(owner ++ ": equation is too large; reduce its size or source")
            else {width:width,height:height,body_top:if (label==null) 0 else (height-body_height)/2,element:if (label==null) measured.element else
                <svg xmlns:"http://www.w3.org/2000/svg",width:width,height:height,viewBox:"0 0 " ++ string(width) ++ " " ++ string(height),
                    <g transform:"translate(0 " ++ string((height-body_height)/2) ++ ")",measured.element>
                    <g transform:"translate(" ++ string(body_width+p.size) ++ " " ++ string((height-label_height)/2) ++ ")",label.element>>}
        }
    }
}
pub fn field_options(p) => {display:p.display==true,font_size:p.size,color:p.color}
