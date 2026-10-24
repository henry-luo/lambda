// baseline exports build fresh model fixtures; UI verification reads the UI's files.
import model: ~~.~~.mod_model
import files: ~~.~~.mod_io
import equation: ~~.~~.mod_equation
import shapes: ~~.~~.mod_shapes
import lambda.editor.mod_doc
import lambda.editor.mod_editor
import lambda.editor.mod_step
import lambda.editor.mod_source_pos
import image: lambda.editor.mod_image

fn equation_editor() {
    let base = edit_set_selection(model.open_editor(node('doc',[node('p',[text("BeforeAfter")])])),
        text_selection(pos([0,0],6),pos([0,0],6)))
    let inserted = model.execute(base,"insert.equation",equation.defaults)^;
    model.execute(inserted,"equation.format",{source:"\\begin{pmatrix}a&b\\\\c&d\\end{pmatrix}",
        number:"(1)",display:true,linear:false,justify:"center",size:28,color:"#805bc5"})^
}

fn shape_editor() {
    let base = files.read_document("test/demo/word/tests/object_layout.word.json")^.editor
    let selected = edit_set_selection(base,node_selection([1,0]));
    model.execute(selected,"shape.format",{values:{text:"Final sign",font:"Georgia",direction:90,
        bold:true,italic:true,fill_style:"gradient",fill2:"#dcebff",dash:"dashed",effect:"shadow",
        opacity:85,alt:"Studio sign"}})^
}

fn group_editor() {
    let base = files.read_document("test/demo/word/tests/object_layout.word.json")^.editor
    let pictures = image.images(base.doc)
    let boxes = [for (i in 0 to 1) {path:pictures[i].path,x:20+i*200,y:30,
        width:160,height:100,object_width:160,object_height:100}];
    model.execute(base,"object.group",{paths:[pictures[0].path,pictures[1].path],boxes:boxes})^
}

pub pn load(feature,title) map^ {
    if (sys.proc.self.env.LAMBDA_WORD_EXPORT_UI == "1") {
        return {saved:files.read_document("temp/word/" ++ feature ++ ".word.json")^,
            imported:files.import_html_document(input("temp/word/" ++ feature ++ ".html",'text')^)^}
    }
    let editor = if (feature == "equations") equation_editor()^
        else if (feature == "shape-format") shape_editor()^ else group_editor()^
    let stem = "temp/word/baseline-" ++ feature
    output(files.archive(editor.doc,title),stem ++ ".word.json",{format:'json',atomic:true})^
    files.export_document(editor.doc,title,stem ++ ".html")^
    return {saved:files.read_document(stem ++ ".word.json")^,
        imported:files.import_html_document(input(stem ++ ".html",'text')^)^}
}
