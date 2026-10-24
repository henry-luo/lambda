import field: ~~.mod_math_field

// LaTeX round-trips through the tree, bracing script arguments so every slot is addressable.
let eq = field.parse_field("a^2+b^2=c^2\\pm\\frac{a}{b}")^
field.source(eq.row) == "a^{2}+b^{2}=c^{2}\\pm \\frac{a}{b}"
field.source(field.parse_field("\\sqrt[3]{x}+\\left(\\alpha\\right)\\text{ if }x_1^{n}")^.row) == "\\sqrt[3]{x}+\\left(\\alpha \\right)\\text{ if }x_{1}^{n}"
field.source(field.parse_field("\\begin{pmatrix}a&b\\end{pmatrix}+1")^.row) == "\\begin{pmatrix}a&b\\end{pmatrix}+1"
field.parse_field("\\frac{a}{") ^ { true } ~ { false }

// The caret token parses into a slot; no slot exists between a base and its script.
let at_two = field.parse_field("a^{\\lambdacaret 2}")^
at_two.caret == {path:[1,0],index:0}
field.source_with_caret(at_two.row,at_two.caret) == "a^{\\lambdacaret 2}"
let ps = field.positions(eq.row)
not any([for (p in ps) p.path==[] and p.index==1])
ps[0] == {path:[],index:0} and ps[last] == {path:[],index:len(eq.row)}

// Left/right walk reading order through numerators and denominators; the ends leave the equation.
let frac = field.parse_field("\\frac{a}{b}")^
let inside = field.move(frac.row,{path:[],index:0},"right")
inside == {path:[0,0],index:0}
field.move(frac.row,field.move(frac.row,inside,"right"),"right") == {path:[0,1],index:0}
field.move(frac.row,{path:[],index:0},"left") == null
field.move(frac.row,{path:[],index:1},"right") == null
field.move(frac.row,{path:[0,1],index:1},"home") == {path:[],index:0}

// Typing: ^ and _ open scripts, / lifts the operand into a numerator, \name resolves on space.
let typed = (src,keys) => reduce([field.parse_field(src)^,*split(keys,"")],(s,ch)=>field.type_text(s.row,s.caret,ch))
field.source(typed("","x^2").row) == "x^{2}"
field.source(typed("","x_1^2").row) == "x_{1}^{2}"
let half = typed("","a/2")
field.source(half.row) == "\\frac{a}{2}"
half.caret == {path:[0,1],index:1}
field.source(typed("","1/").row) == "\\frac{1}{}"
field.source(typed("","x+/").row) == "x+\\frac{}{}"
field.source(typed("","\\alpha +1").row) == "\\alpha +1"
let root = typed("","\\sqrt x")
field.source(root.row) == "\\sqrt{x}"
root.caret == {path:[0,0],index:1}
field.source(typed("","\\text ab c").row) == "\\text{ab c}"
field.source(typed("","{%").row) == "\\{\\%"
field.source(typed("","\\al").row) == "\\mathrm{\\backslash al}"

// Backspace enters a structure, deletes inside it, then removes the emptied structure.
let pow = field.parse_field("x^{2}\\lambdacaret ")^
let into = field.erase(pow.row,pow.caret,false)
into.caret == {path:[1,0],index:1}
let gone = field.erase(into.row,into.caret,false)
field.source(gone.row) == "x^{}"
let base = field.erase(gone.row,gone.caret,false)
field.source(base.row) == "x"
base.caret == {path:[],index:1}
field.source(field.erase(base.row,base.caret,false).row) == ""

// Ribbon templates insert at the caret and keep typing where the template's caret token sits.
let built = field.insert_source(frac.row,{path:[0,1],index:1},"+\\sqrt{\\lambdacaret }")^
field.source(built.row) == "\\frac{a}{b+\\sqrt{}}"
built.caret == {path:[0,1,2,0],index:0}

// One render reports every slot; fractions stack slots vertically.
let g = field.geometry(frac.row,{display:true,font_size:20,color:"#000000"})^
len(g) == len(field.positions(frac.row))
let num = field.caret_box(g,{path:[0,0],index:0})
let den = field.caret_box(g,{path:[0,1],index:0})
;(num.bottom <= den.top + 4 and num.top < den.top)
field.vertical(frac.row,g,{path:[0,0],index:1},true) == {path:[0,1],index:1}
field.vertical(frac.row,g,{path:[0,1],index:0},false) == {path:[0,0],index:0}
field.nearest(g,den.x,den.top+2) == {path:[0,1],index:0}
let subsup = field.parse_field("x_{1}^{2}")^
let sg = field.geometry(subsup.row,{display:true,font_size:20,color:"#000000"})^
field.vertical(subsup.row,sg,{path:[1,0],index:1},true) == null
field.vertical(subsup.row,sg,{path:[1,0],index:1},false) == {path:[1,1],index:1}
field.vertical(subsup.row,sg,{path:[1,1],index:0},true) == {path:[1,0],index:0}
let wide = field.geometry(eq.row,{display:true,font_size:20,color:"#000000"})^
let xs = [for (p in [{path:[],index:0},{path:[],index:2},{path:[],index:4}]) field.caret_box(wide,p).x]
;(xs[0] < xs[1] and xs[1] < xs[2])
