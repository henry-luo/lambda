// run after utils/spelling_vocab_poc.py; not a default corpus test.
import vocab: lambda.doc.spelling.vocabulary

pn main() {
    let report = input("temp/spelling_vocab_poc/out/measurements.json","json")^
    var checks = []
    for (profile in report.profiles) {
        let words = vocab.read("temp/spelling_vocab_poc/out/" ++ profile.formats.rows.file)^
        checks = [*checks,[
            len(words.entries) == profile.entries,
            all([for (word in ["cat","cats","dog","dogs"]) vocab.lookup(words,word)^[0] == word]),
            vocab.lookup(words,"zzzznotawordzzzz")^ == null,
            vocab.lookup(words,words.keys[0])^ == words.entries[0],
            vocab.lookup(words,words.keys[len(words.keys)-1])^ == words.entries[len(words.entries)-1]
        ]]
    }
    return checks
}
