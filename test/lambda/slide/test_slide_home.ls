import m: ~~.~~.demo.slides.mod_model
import h: ~~.~~.demo.slides.mod_home
import shared: ~~.~~.demo.mod_text_format
import css: lambda.editor.mod_inline_format

let base = {*:m.initial(),selected:"s1-o1",index:1}
let sized = h.property(base,"font_size","28")^
let grown = h.command(sized,"grow-font")^
let family = h.property(grown,"font-family","Georgia")^
let bold = h.command(family,"bold")^
let underlined = h.command(bold,"underline")^
let struck = h.command(underlined,"strike")^
let uppercase = h.command(struck,"case:upper")^
let bullets = h.command(uppercase,"bullets")^
let numbered = h.command(bullets,"numbering:upper-alpha")^
let vertical = h.command(numbered,"vertical:middle")^
let edited = m.set_text(vertical,"First\nSecond")^
let spaced = h.command(edited,"spacing:2")^
let indented = h.command(spaced,"indent")^
let scripted = h.command(indented,"superscript")^
let unscripted = h.command(scripted,"superscript")^
let painted = h.paint({*:m.select_slide(h.painter(spaced),0),selected:"s0-o2"})^
let cleared = h.command(struck,"clear-format")^
let shaped = m.add_object(base,"rect")^
let styled = h.command(shaped,"shape-style:violet")^
let right = h.command(styled,"object-align:right")^
let cut = h.command(right,"cut")^
let pasted = m.paste(cut)^
let text_paste = h.command({*:base,clipboard:m.copy_object(struck)},"paste-text")^
let reset_base = m.properties(base,{x:200.0,y:300.0,width:500.0})^
let reset = h.command(reset_base,"reset-layout")^
let rich = {*:m.initial(<presentation <slide <text x:10.0,width:400.0,<b "hello "><span "world">>>>),selected:"s0-o0"}
let rich_case = h.command(rich,"case:title")^;
let highlight = h.command(sized,"highlight:#f9e896")^
let highlight_edit = m.set_text(highlight,"Highlighted")^
let highlighted_list = h.command(highlight,"bullets")^
let list_edit = m.set_text(highlighted_list,"Revised\nItems")^
let circle = m.add_object(base,"circle")^
let plain_paste = h.command({*:base,clipboard:m.copy_object(numbered)},"paste-text")^;
{
    font:[m.chosen(grown).font_size == 36.0,h.family(family) == "Georgia",h.pressed(bold,"bold"),
        h.pressed(underlined,"underline"),h.pressed(struck,"underline") and h.pressed(struck,"strike"),
        m.text(m.chosen(uppercase).source) == upper(m.text(m.chosen(struck).source)),
        shared.font_step(100,true) >= 100,shared.font_step(4,false) <= 4],
    paragraphs:[h.list_kind(bullets) == 'ul',h.list_kind(numbered) == 'ol',
        len(content(h.list_node(m.chosen(numbered).source))) == 3,
        m.text(m.chosen(edited).source) == "First\nSecond",m.plain(m.chosen(edited).source),
        h.value(spaced,"line-height") == "2",h.value(indented,"padding-left") == "24px",
        h.pressed(scripted,"superscript"),not h.pressed(unscripted,"superscript"),
        m.text(m.chosen(rich_case).source) == "Hello World" and name(content(m.chosen(rich_case).source)[0]) == 'b'],
    painter:[m.chosen(painted).font_size == m.chosen(spaced).font_size,
        m.chosen(painted).x == 84.0,painted.painter == false],
    clear:[not h.pressed(cleared,"bold"),not h.pressed(cleared,"underline"),
        m.chosen(cleared).x == m.chosen(struck).x],
    drawing:[m.chosen(styled).source.fill == "#805bc5",m.chosen(right).x+m.chosen(right).width == right.plan.width,
        len(m.scene(cut).objects) == len(m.scene(right).objects)-1,
        m.chosen(pasted).source.fill == "#805bc5",len(text_paste.undo) == len(base.undo)+1],
    layout:[m.chosen(reset).x == m.chosen(base).x,m.text(m.chosen(reset).source) == m.text(m.chosen(base).source),
        (h.command(m.initial(),"reset-layout")^).doc == m.initial().doc],
    persistence:[m.initial(parse(format(edited.doc,'mark'),'mark')^).doc == edited.doc,m.history(bold).doc == family.doc],
    highlight:[m.plain(m.chosen(highlight).source),m.text(m.chosen(highlight_edit).source) == "Highlighted",
        content(m.chosen(highlight_edit).source)[0].class == "slide-text-highlight",
        len(content(h.list_node(m.chosen(highlighted_list).source))) == 3,
        h.list_node(m.chosen(highlighted_list).source)[0][0].class == "slide-text-highlight",
        h.list_node(m.chosen(list_edit).source)[0][0].class == "slide-text-highlight"],
    plain_paste:[h.list_kind(plain_paste) == null,m.plain(m.chosen(plain_paste).source),m.text(m.chosen(plain_paste).source) == m.text(m.chosen(numbered).source)],
    circle:m.chosen(circle).width == m.chosen(circle).height,
    invalid:[h.property(base,"font_size","0") ^ {true} ~ {false},h.property(base,"font-family","Arial;bad") ^ {true} ~ {false}]
}
