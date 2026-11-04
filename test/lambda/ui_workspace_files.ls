import browser: lambda.ui.workspace.file_browser

let entries = [
    {name:"notes.txt",path:"/one/notes.txt",dir:false,kind:"Text document",size:15},
    {name:"Folder",path:"/one/Folder",dir:true,kind:"Folder",size:0},
    {name:".secret",path:"/one/.secret",dir:false,kind:"Document",size:1}]
let first = browser.listing(browser.initial("/one"),"/one",entries)^
let second = browser.listing(browser.initial("/two"),"/two",[{name:"Other",path:"/two/Other",dir:true}])^
let chosen = browser.choose(first,"/one/notes.txt")
let filtered = browser.reconcile({*:chosen,query:"folder"})^
let child = browser.listing(chosen,"/one/Folder",[])^
let back = browser.listing(child,"/one",entries,0)^;
[
    len(browser.visible(first)^)==2 and browser.visible(first)^[0].name=="Folder",
    browser.focused(chosen).name=="notes.txt" and len(second.selected)==0,
    filtered.selected==[] and filtered.focus=="",
    child.history==["/one","/one/Folder"] and child.cursor==1,
    back.history==child.history and back.cursor==0 and back.selected==[],
    second.path=="/two" and browser.visible(second)^[0].name=="Other"
]
