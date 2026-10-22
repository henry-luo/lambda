// Keep editable LaTeX beside a self-contained typeset SVG (D7.2.5).
import mathdoc: lambda.doc.math.math
import objects: .mod_objects
import colors: lambda.ui.core.component

pub let examples = [{label:"Quadratic formula",source:"x=\\frac{-b\\pm\\sqrt{b^2-4ac}}{2a}"},
    {label:"Pythagorean theorem",source:"a^2+b^2=c^2"},
    {label:"Integral",source:"\\int_0^1 x^2\\,dx=\\frac{1}{3}"},
    {label:"Sum",source:"\\sum_{k=1}^{n}k=\\frac{n(n+1)}{2}"}]
pub let defaults = {source:"a^2+b^2=c^2",size:22,color:"#24334b",display:true}
pub fn settings(n) => objects.settings(n,"data-word-equation")
pub fn transaction(editor,p,selected=null) map^ {
    if (not (p.source is string) or trim(p.source)=="" or len(p.source)>4000 or
        not (p.size is number) or p.size<10 or p.size>72 or not colors.hex_color(p.color) or not (p.display is bool))
        raise error("Word: enter a LaTeX equation (up to 4000 characters), size 10–72 px, and a #RRGGBB color")
    else {
        let ast = parse(p.source,{type:"math",flavor:"latex"})^
        let unknown = mathdoc.unmapped_commands(ast)
        if (len(unknown)>0) raise error("Word: unsupported equation command: " ++ join(unknown,", ")) else {
            let measured = mathdoc.render_box(ast,{display:p.display==true,font_size:p.size,color:p.color})^
            let width = max(1,ceil(measured.width*p.size))
            let height = max(1,ceil((measured.height+measured.depth)*p.size))
            if (width>650 or height>1000) raise error("Word: equation is too large; reduce its size or source")
            else objects.transaction(editor,"data-word-equation",p,measured.element,width,height,p.source,selected)^
        }
    }
}
