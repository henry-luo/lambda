import storage: ~~.mod_io
import model: ~~.mod_model
import sample: ~~.mod_sample
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

let book = sample.notebook()
let encoded = storage.encode(book)
let decoded = storage.decode(encoded)^;
decoded==book
contains(encoded,"lambda-notes") and contains(encoded,"version")
storage.descriptor.check_roundtrip(book,{})==null
let exact = {*:book,notes:[{*:book.notes[0],doc:node('doc',[model.paragraph("  café & <tea>\n  两杯 ☕  ")])}]};
storage.decode(storage.encode(exact))^==exact
let colored_editor = edit_set_selection(model.open_editor(model.blank()),text_selection(pos([0,0],0),pos([0,0],0)))
let colored = model.format_color(model.format_color(colored_editor,"color","#356eb5")^,"highlight","#fff09b")^
let colorful = {*:book,notes:[{*:book.notes[0],doc:edit_exec(colored,edit_cmd_insert_text("A colorful thought")).doc}]};
storage.decode(storage.encode(colorful))^==colorful
let deleted = model.snapshot(model.change_note(model.initial(book),{deleted:true}));
storage.decode(storage.encode(deleted))^==deleted
let malformed = parse(encoded,'json')^;
(storage.decode("not json") ^ { null })==null;
(storage.decode(format({*:malformed,version:2},'json')^) ^ { null })==null;
(storage.decode(format({*:malformed,folders:[]},'json')^) ^ { null })==null;
(storage.decode(format({*:malformed,notes:[malformed.notes[0],malformed.notes[0]]},'json')^) ^ { null })==null;
(storage.decode(format({*:malformed,notes:[{*:malformed.notes[0],folder:"missing"}]},'json')^) ^ { null })==null;
(storage.decode(format({*:malformed,notes:[{*:malformed.notes[0],doc:"{kind: 'node', tag: 'script', attrs: [], content: []}"}]},'json')^) ^ { null })==null;
(storage.decode(format({*:malformed,sequence:0},'json')^) ^ { null })==null;
(storage.read_book("wrong.txt") ^ { null })==null
