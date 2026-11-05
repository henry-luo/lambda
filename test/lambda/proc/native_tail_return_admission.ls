// An imported native-return procedure must admit its untyped tail parameter
// before converting to a string pointer (S11.4.2v3, D2.3.4–D2.3.5).
import store: lambda.mail.store

pn main() {
    let root = "temp/native-tail-return-" ++ (uuid()^)
    let path = root ++ "/index/inbox.mark"
    var correct = true
    for (i in 0 to 9) {
        let saved = store.save_index(path, {entries: [], revision: i})^
        correct = correct and saved == path and (input(path, 'mark')^).revision == i
    }
    print(correct)
    print("\n")
    io.delete(root)^
}
