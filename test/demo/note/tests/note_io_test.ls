import storage: ~~.mod_io
import model: ~~.mod_model
import sample: ~~.mod_sample
import lambda.editor.mod_doc
import lambda.editor.mod_step

let book = sample.notebook()
let encoded = storage.encode(book)
let decoded = storage.decode(encoded)^;
decoded==book
contains(encoded,"lambda-notes") and contains(encoded,"version")
storage.descriptor.check_roundtrip(book,{})==null
let exact = {*:book,notes:[{*:book.notes[0],doc:node('doc',[model.paragraph("  café & <tea>\n  两杯 ☕  ")])}]};
storage.decode(storage.encode(exact))^==exact
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
