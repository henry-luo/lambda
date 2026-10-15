// Core word-processing sample and styles, adapted from WordCraft's sample.rs.
// Copyright (c) 2026 ArtCraft Team and the WordCraft contributors; see LICENSE-MIT.
import lambda.editor.mod_doc
import lambda.editor.mod_step

pub let styles = [
    {id:"Normal",label:"Normal",tag:'p'},
    {id:"Title",label:"Title",tag:'h1'},
    {id:"Subtitle",label:"Subtitle",tag:'p'},
    {id:"Heading1",label:"Heading 1",tag:'h2'},
    {id:"Heading2",label:"Heading 2",tag:'h3'},
    {id:"Quote",label:"Quote",tag:'p'}
]
pub let table_styles = [
    {id:"grid",label:"Simple grid"}, {id:"soft",label:"Banded blue"},
    {id:"contrast",label:"Dark header"}, {id:"plain",label:"Minimal lines"}
]
pub fn paragraph(words,style="Normal") =>
    node_attrs('p',[{name:'data-word-style',value:style}],[text(words)])
fn styled(words,style,tag) => node_attrs(tag,[{name:'data-word-style',value:style}],[text(words)])
fn cell(words,header) => node(if (header) 'th' else 'td',[text(words)])
fn row(words,header=false) => node('tr',[for (s in words) cell(s,header)])
pub fn blank() => node('doc',[paragraph("")])
pub fn sample() => node('doc',[
    styled("The Open Studio Handbook","Title",'h1'),
    styled("A place for people who make things","Subtitle",'p'),
    node('p',[
        text("Every Thursday evening the old print shop on Alder Street fills with people who make things. "),
        text_marked("Painters",[mark('strong',true)]),
        text(" set up by the north windows, "),
        text_marked("letterpress",[mark('em',true)]),
        text(" regulars claim the back room, and somebody always brings bread.")]),
    styled("Why share a studio?","Heading1",'h2'),
    paragraph("Rent is the obvious reason, but it is not the best one. A shared room puts your work next to work you would never have seen otherwise. The cross-talk is the point."),
    paragraph("A studio is a place where unfinished things are welcome.","Quote"),
    styled("What we agree on","Heading2",'h3'),
    node('ul',[for (s in ["Clean as you go. Leave a table ready for the next maker.",
        "Label what is yours. Share what is on the commons shelf.",
        "Ask before you borrow. A quick note helps everyone keep track."])
        node('li',[paragraph(s)])]),
    styled("Shared equipment","Heading2",'h3'),
    node('table',[
        row(["Equipment","Location","Steward"],true),
        row(["Etching press","Back room","Mara O."]),
        row(["Kiln","Yard shed","Theo P."]),
        row(["Light table","North windows","Everyone"])]),
    styled("Make yourself at home","Heading1",'h2'),
    paragraph("Start with a small idea. Ask a question. Leave room for something unexpected. If you have never made anything before, you are exactly who we hoped would show up.")
])
