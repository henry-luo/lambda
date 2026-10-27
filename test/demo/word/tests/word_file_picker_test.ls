import picker: ~~.mod_file_picker
import browser: ~~.~~.finder.mod_model
import storage: ~~.mod_io

let folder = browser.absolute("test/demo/word/tests",sys.proc.self.cwd#)
let d = picker.initial("",folder)
let entries = [
    {name:"Folder",path:"/demo/Folder",dir:true,kind:"Folder",size:0},
    {name:"Notes.md",path:"/demo/Notes.md",dir:false,kind:"Text document",size:20},
    {name:"Report.WORD.JSON",path:"/demo/Report.WORD.JSON",dir:false,kind:"Data file",size:40},
    {name:"picture.svg",path:"/demo/picture.svg",dir:false,kind:"Image",size:60},
    {name:".hidden.txt",path:"/demo/.hidden.txt",dir:false,kind:"Text document",size:10}]
let sample = {*:d,browser:browser.listing(browser.initial("/demo"),"/demo",entries)}
let note = picker.choose(sample,"/demo/Notes.md")
let subfolder = picker.choose(sample,"/demo/Folder")
let searched = picker.command(note,"search",{value:"REPORT"})
let typed = picker.command(sample,"filename",{value:"Notes.md"})
let parent = picker.command(d,"up")
let back = picker.command(parent,"back")
let forward = picker.command(back,"forward")
let failure = picker.navigate(note,"/a-folder-that-does-not-exist/lambda-word-picker")
let child = picker.command(d,"navigate",{path:"proc"});
let spaced = {*:sample,browser:browser.listing(sample.browser,"/demo",[
    {name:" Notes # ü.md",path:"/demo/ Notes # ü.md",dir:false,kind:"Text document",size:20}])};
[
    all([for (p in ["a.word.json","a.WORD.JSON","a.html","a.htm","a.md","a.markdown","a.txt","a.docx"]) storage.supported_path(p)]),
    all([for (p in ["a.json","a.pdf","a.svg","a.png","a.ls","a.word.json.bak",""]) not storage.supported_path(p)]),
    picker.rows(sample)[0].dir,
    len(picker.rows(sample))==4,
    len(picker.rows(picker.command(sample,"hidden")))==5,
    not picker.can_open(sample),
    picker.can_open(note) and picker.selected(note).name=="Notes.md",
    note.path=="Notes.md" and picker.target(note)=="/demo/Notes.md",
    picker.command(note,"open").open_path=="/demo/Notes.md",
    picker.command(sample,"activate",{path:"/demo/Report.WORD.JSON"}).open_path=="/demo/Report.WORD.JSON",
    picker.choose(sample,"/demo/picture.svg")==sample,
    picker.command(sample,"activate",{path:"/demo/picture.svg"}).open_path==null,
    picker.can_open(subfolder) and subfolder.path=="",
    len(picker.rows(searched))==1 and searched.path=="" and picker.selected(searched)==null,
    picker.command(sample,"move",{delta:1}).browser.focus=="/demo/Folder",
    picker.command(subfolder,"move",{delta:1}).browser.focus=="/demo/Notes.md",
    picker.command(sample,"move",{edge:"last"}).browser.focus=="/demo/Report.WORD.JSON",
    picker.command(note,"move",{edge:"first"}).browser.focus=="/demo/Folder",
    picker.command(sample,"mode",{value:"list"}).browser.mode=="list",
    picker.command(sample,"sort",{value:"size"}).browser.sort=="size",
    picker.command(sample,"go").go and not picker.command(picker.command(sample,"go"),"go").go,
    picker.target(typed)=="/demo/Notes.md" and picker.can_open(typed),
    picker.command(note,"filename",{value:""}).browser.selected==[],
    d.error==null and len(d.browser.entries)>0 and d.browser.path==folder,
    parent.browser.path==browser.parent(folder),
    back.browser.path==folder and back.browser.history==parent.browser.history,
    forward.browser.path==parent.browser.path,
    picker.command(d,"back")==d,
    picker.command(d,"refresh").browser.history==d.browser.history,
    failure.error!=null and failure.browser.path==note.browser.path and failure.browser.selected==note.browser.selected,
    child.browser.path==folder ++ "/proc" and child.path=="",
    picker.command(child,"back").browser.path==folder,
    picker.command(d,"activate",{path:folder ++ "/proc"}).browser.path==child.browser.path,
    picker.navigate(d,"import.md").error!=null,
    len([for (p in picker.places() where p.id=="home" and p.path==sys.home#) p])==1,
    picker.command(picker.choose(spaced,"/demo/ Notes # ü.md"),"open").open_path=="/demo/ Notes # ü.md"
]
