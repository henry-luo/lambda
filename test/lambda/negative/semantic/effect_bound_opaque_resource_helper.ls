// S12.1.6v2: an opaque legacy fn call cannot prove its resource bound.
fn reader(path) any^ => input(path)^
fn hidden_reader(path) any^ {
    let readers = [reader];
    readers[0](path)^
}
pn bounded(path) any^ @clock { hidden_reader(path)^ }
