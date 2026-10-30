import spelling: lambda.doc.spelling
import front: lambda.doc.spelling.front_coded
import incremental: lambda.doc.spelling.incremental
import lambda.editor.mod_doc
import lambda.editor.mod_step

fn checked(checker,units) => incremental.accept(units,[for (request in incremental.pending(units))
    {*:request,report:spelling.check(checker,request.text)^}])

pn main() {
    let dictionary = front.from_bytes(binary("a|first|hello|is|last|middle|paragraph|text|the|this|word\n"))^
    let checker = {language:"en-US",dictionary:dictionary,words:[]}
    let source = node('doc',[node('p',[text("teh first")]),node('p',[text("hello middle")]),node('p',[text("speling last")])])
    let initial = incremental.refresh(source)^
    let cached = checked(checker,initial)^
    let edited_source = replace_node_at(source,[1],node('p',[text("teh middle")]))
    let edited = incremental.refresh(edited_source,cached)^
    let current = checked(checker,edited)^
    let changed = incremental.pending(edited)
    let formatted = incremental.refresh(replace_node_at(edited_source,[1],
        node('p',[text("te"),text_marked("h",[mark('strong',true)]),text(" middle")])),current)^
    let growing = incremental.refresh(replace_node_at(edited_source,[1],node('p',[text("teh middle text")])),edited)^
    let late = incremental.accept(growing,[{*:changed[0],report:spelling.check(checker,changed[0].text)^}])
    let corrected = incremental.refresh(replace_node_at(edited_source,[1],node('p',[text("the middle text")])),late)^
    let extended_word = incremental.refresh(replace_node_at(edited_source,[1],node('p',[text("tehh middle")])),current)^
    let undo = incremental.accept(incremental.refresh(edited_source,corrected)^,
        [{key:changed[0].key,text:"\nthe middle text\n",report:spelling.check(checker,"\nthe middle text\n")^}])
    let inserted_source = node('doc',[node('p',[text("a paragraph")]),*source.content])
    let inserted = incremental.refresh(inserted_source,cached)^
    let deleted = incremental.refresh(node('doc',slice(source.content,1)),cached)^
    let split_units = incremental.refresh(node('doc',[source.content[0],node('p',[text("hello")]),
        node('p',[text("middle")]),source.content[2]]),cached)^
    let excluded = incremental.refresh(node('doc',[
        node_attrs('div',[{name:'lang',value:"fr"}],[node('p',[text("teh")])]),
        node('code',[text("speling")]),
        node_attrs('p',[{name:'spellcheck',value:false}],[text("teh")]),
        node('p',[text_marked("speling",[mark('code',true)])]),
        node('p',[text("hello")])]))^
    let loose = incremental.refresh(<div <span "te">;<strong "h">;<p "hello">;"spe";<em "ling">>)^
    let loose_checked = checked(checker,loose)^
    let plain = checked(checker,incremental.refresh("teh first\nhello middle\nspeling last")^)^
    let plain_edited = incremental.refresh("teh first\nhello middle text\nspeling last",plain)^
    let plain_report = incremental.report(plain_edited)
    let suffix_base = checked(checker,incremental.refresh(node('p',[text("teh speling")]))^)^
    let suffix_edit = incremental.refresh(node('p',[text("the 😀 speling")]),suffix_base)^
    let suffix_report = incremental.report(suffix_edit)
    let combined_word = incremental.refresh(node('p',[text("teh's speling")]),suffix_base)^
    let loading_source = node('doc',[for (i in 0 to 11) node('p',[text("hello word")])])
    let loading = incremental.refresh(loading_source)^
    let priority = incremental.refresh(replace_node_at(loading_source,[11],node('p',[text("teh word")])),loading)^
    let cases = [
        {name:"initial paragraphs",ok:len(initial)==3 and len(incremental.pending(initial))==3},
        {name:"completed cache",ok:incremental.pending(cached)==[]},
        {name:"one changed paragraph",ok:len(changed)==1 and changed[0].key=="[1]" and trim(changed[0].text)=="teh middle"},
        {name:"unchanged source paths",ok:incremental.report(edited).issues[0].ranges==[{path:[0,0],start:0,end:3}]},
        {name:"unchanged underlines survive",ok:[for (i in incremental.report(edited).issues) i.word]==["teh","speling"]},
        {name:"changed results merge",ok:[for (i in incremental.report(current).issues) i.word]==["teh","teh","speling"]},
        {name:"formatting reuses spelling",ok:incremental.pending(formatted)==[]},
        {name:"formatting remaps inline ranges",ok:incremental.report(formatted).issues[1].ranges==
            [{path:[1,0],start:0,end:2},{path:[1,1],start:0,end:1}]},
        {name:"late prefix result paints while typing",ok:len(incremental.report(late).issues)==3},
        {name:"late result leaves changed paragraph pending",ok:len(incremental.pending(late))==1},
        {name:"correction removes stale issue",ok:[for (i in incremental.report(corrected).issues) i.word]==["teh","speling"]},
        {name:"extending a word removes stale issue",ok:[for (i in incremental.report(extended_word).issues) i.word]==["teh","speling"]},
        {name:"undo ignores incompatible late result",ok:incremental.pending(undo)==[] and len(incremental.report(undo).issues)==3},
        {name:"insertion checks new paragraph only",ok:len(incremental.pending(inserted))==1 and incremental.pending(inserted)[0].key=="[0]"},
        {name:"insertion rebases cached ranges",ok:incremental.report(inserted).issues[0].ranges[0].path==[1,0]},
        {name:"deletion reuses remaining paragraphs",ok:incremental.pending(deleted)==[] and len(incremental.report(deleted).issues)==1},
        {name:"deletion rebases cached ranges",ok:incremental.report(deleted).issues[0].ranges[0].path==[1,0]},
        {name:"split checks the two new paragraphs",ok:len(incremental.pending(split_units))==2},
        {name:"split keeps following cached issue",ok:incremental.report(split_units).issues[1].ranges[0].path==[3,0]},
        {name:"excluded prose omitted",ok:len(incremental.pending(excluded))==1 and trim(incremental.pending(excluded)[0].text)=="hello"},
        {name:"inline groups preserve complete words",ok:[for (i in incremental.report(loose_checked).issues) i.word]==["teh","speling"]},
        {name:"inline groups retain authored paths",ok:incremental.report(loose_checked).issues[1].ranges==
            [{path:[3],start:0,end:3},{path:[4,0],start:0,end:4}]},
        {name:"plain text edits check one line",ok:len(incremental.pending(plain_edited))==1},
        {name:"plain text cached offsets follow edits",ok:plain_report.issues[1].start==28 and plain_report.issues[1].end==35},
        {name:"unchanged suffix survives correction",ok:[for (i in suffix_report.issues) i.word]==["speling"]},
        {name:"suffix offsets use code points",ok:suffix_report.issues[0].ranges==[{path:[0],start:6,end:13}]},
        {name:"apostrophe extension invalidates old word",ok:[for (i in incremental.report(combined_word).issues) i.word]==["speling"]},
        {name:"typing outranks initial background scan",ok:incremental.pending(priority)[0].key=="[11]"},
        {name:"email context does not retain issue",ok:not spelling.issue_present("teh@example.com",{word:"teh",start:0,end:3})},
        {name:"word boundary checked",ok:not spelling.issue_present("tehh",{word:"teh",start:0,end:3})}
    ];
    return [for (test in cases where not test.ok) test.name]
}
