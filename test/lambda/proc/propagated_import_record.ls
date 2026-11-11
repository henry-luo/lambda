import m: ~~.mod_propagated_import_record

pn main() {
    let saved = m.produced({ok:true})^
    let recovered = m.produced({ok:false},true) ^ { {message:^.message} };
    [saved.ok,saved.source.path,recovered.message]
}
