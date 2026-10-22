import front: lambda.doc.spelling.front_coded

pn main() {
    let manifest = input("temp/spelling_vocab_poc/front/manifest.json","json")^
    let dictionary = front.read(manifest.dictionary,manifest.entries)^
    let oracle = input(manifest.oracle,"json")^;
    [
        dictionary.count == manifest.entries,
        len(dictionary.offsets) == manifest.lines,
        len(dictionary.bytes) == manifest.bytes,
        front.verify(dictionary,oracle.words)^,
        all([for (i,word in oracle.words where i % 64 == 0 or i % 64 == 63 or i == len(oracle.words) - 1)
            front.lookup(dictionary,word)^ == i]),
        all([for (i,word in oracle.negatives where i % 16 == 0) front.lookup(dictionary,word)^ == null])
    ]
}
