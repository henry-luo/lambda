import model: ~~.mod_model
import lookup: ~~.mod_lookup
import lambda.editor.mod_doc
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

// request URLs
[lookup.dictionary_url(" Hello "),lookup.translate_url("How are you?","zh-CN")]
;[lookup.language_name("ja"),lookup.language_name("xx")]

// caret words and lookup sources
;[lookup.word_at("Hello brave world",8),lookup.word_at("Hello brave world",5),lookup.word_at("a  b",2)]
let doc = node('doc',[node('p',[text("Hello brave world")]),node('p',[text("Second line")])])
let editor = model.open_editor(doc)
let caret = edit_set_selection(editor,text_selection(pos([0,0],8),pos([0,0],8)))
lookup.source(caret,"dictionary")
lookup.source(caret,"translate").text
lookup.source(caret,"translate").selection == text_selection(pos([0,0],0),pos([0,0],17))
let picked = edit_set_selection(editor,text_selection(pos([0,0],12),pos([1,0],6)))
;[lookup.source(picked,"dictionary").text,lookup.source(picked,"translate").text]

// dictionary answers
let hello = "[{\"word\":\"hello\",\"phonetics\":[{\"audio\":\"x.mp3\"},{\"text\":\"/həˈləʊ/\"}]," ++
    "\"meanings\":[{\"partOfSpeech\":\"noun\",\"definitions\":[{\"definition\":\"A greeting.\",\"synonyms\":[\"hi\"],\"antonyms\":[]}],\"synonyms\":[\"greeting\",\"hi\"]}," ++
    "{\"partOfSpeech\":\"interjection\",\"definitions\":[{\"definition\":\"Said on meeting.\",\"example\":\"Hello, everyone.\"}]}]}]"
lookup.read_dictionary(hello)
lookup.read_dictionary("{\"title\":\"No Definitions Found\"}")
lookup.read_dictionary("not json")
let ask = lookup.request(lookup.pane("dictionary","qwzxv",null))
ask
lookup.outcome(ask,{ok:false,status:404,body:"{\"title\":\"No Definitions Found\"}"})
lookup.outcome(ask,{ok:false,status:503,body:""})
lookup.outcome(ask,{ok:true,status:200,body:hello}).result.word

// translation answers
let hola = "[[[\"Hola Mundo. \",\"Hello world. \",null,null,10],[\"¿Cómo estás?\",\"How are you?\",null,null,12]],null,\"en\",null,null,null,0.98]"
lookup.read_translation(hola)
lookup.read_translation("[null]")
let pane = lookup.pane("translate","Hello world. How are you?",{target:"fr",seq:4})
lookup.request(pane).url
lookup.outcome(lookup.request(pane),{ok:true,status:200,body:hola})
lookup.outcome(lookup.request(pane),{ok:true,status:200,body:"<html>"})

// only the latest request's report is shown
let waiting = {*:pane,seq:5,loading:true}
lookup.receive(waiting,{seq:4,result:{text:"old"}})
lookup.receive(waiting,{seq:5,result:{text:"Bonjour"}}).result
lookup.receive(null,{seq:5,error:"late"})
