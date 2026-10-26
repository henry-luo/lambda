import spelling: lambda.doc.spelling
import front: lambda.doc.spelling.front_coded
import document: lambda.doc.spelling.document
import async: lambda.edit.spelling
import lambda.editor.mod_doc
import lambda.editor.mod_step

pn main() {
    let dictionary = front.from_bytes(binary("Lambda|NASA|a|café|can't|cat;s|dog;s|hello|is|spelling|test|the|this|word\n"))^
    let checker = {language:"en-US",dictionary:dictionary,words:[]}
    let source = "😀 cafe\u0301 teh speling 中文"
    let checked = spelling.check(checker,source)^
    let doc = node('doc',[node('p',[text("this is te"),text_marked("h",[mark('strong',true)])]),
        node('code',[text("codezz")]),node_attrs('p',[{name:'lang',value:"fr"}],[text("bonjourzz")]),
        node_attrs('p',[{name:'spellcheck',value:false}],[text("ignoredzz")]),node('p',[text("cat-dgo")])])
    let report = document.check(checker,doc)^
    let installed = spelling.load()^;
    var unavailable = ""
    spelling.load("en-GB") ^ { unavailable = ^.message }
    return [
        spelling.tokens("")==[],
        spelling.tokens("'cat' dog’s cat-dog")[0]=={word:"cat",start:1,end:4},
        spelling.correct(checker,"hello")^,
        spelling.correct(checker,"Hello")^,
        spelling.correct(checker,"HELLO")^,
        not spelling.correct(checker,"hElLo")^,
        spelling.correct(checker,"NASA")^,
        not spelling.correct(checker,"nasa")^,
        spelling.correct(checker,"can’t")^,
        spelling.correct(checker,"cafe\u0301")^,
        spelling.correct(spelling.with_words(checker,["Radiant"]),"Radiant")^,
        not spelling.correct(checker,"Radiant")^,
        checked.issues==[{word:"teh",start:8,end:11},{word:"speling",start:12,end:19}],
        checked.skipped==1,
        spelling.check(checker,"this https://speling.example/teh is help@example.com; 4bn cat.")^.issues==[],
        report.issues[0].word=="teh",
        report.issues[0].ranges==[{path:[0,0],start:8,end:10},{path:[0,1],start:0,end:1}],
        [for (i in report.issues) i.word]==["teh","dgo"],
        report.issues[1].ranges==[{path:[4,0],start:4,end:7}],
        document.check(checker,<p "te";<strong "h">>)^.issues[0].ranges==
            [{path:[0],start:0,end:2},{path:[1,0],start:0,end:1}],
        document.check(checker,<p "cat";<code "ignoredzz">;"dog">)^.issues==[],
        async.current(4,{seq:4}),
        not async.current(5,{seq:4}),
        installed.language=="en-US" and installed.dictionary.count==109902,
        spelling.check(installed,"This is teh speling test.")^.issues==
            [{word:"teh",start:8,end:11},{word:"speling",start:12,end:19}],
        unavailable=="Spelling: only en-US is installed"
    ]
}
