import front: lambda.doc.spelling.front_coded

// Fixture-only ASCII punctuation policy; this POC does not implement the full prose tokenizer.
fn tokens(text: string) array^ => split(lower(replace(text,\("." | "," | ":"),"")),null)^
fn check(dictionary, paragraph: string) array^ =>
    [for (word in tokens(paragraph)^ where front.lookup(dictionary,word)^ == null) word]

pn main() {
    let manifest = input("temp/spelling_vocab_poc/front/manifest.json","json")^
    let paragraph = input("test/input/spelling/paragraph_100.txt","text")^
    let read_start = clock()
    let bytes = input(manifest.dictionary,"binary")^
    let read_end = clock()
    let dictionary = front.from_bytes(bytes,manifest.entries)^
    let load_end = clock()
    let first_start = clock()
    let missing = check(dictionary,paragraph)^
    let first_end = clock()
    output({load_ms:(load_end-read_start)*1000.0,first_ms:(first_end-first_start)*1000.0,
        unknown:missing},"temp/spelling_vocab_poc/front/benchmark-progress.json",{format:"json"})^
    let repeats: int = 3
    var checksum: int = 0
    var i: int = 0
    while (i < 5) {
        checksum = checksum + len(check(dictionary,paragraph)^)
        i = i + 1
    }
    var samples = []
    var sample: int = 0
    while (sample < 9) {
        let start = clock()
        i = 0
        while (i < repeats) {
            checksum = checksum + len(check(dictionary,paragraph)^)
            i = i + 1
        }
        samples.push((clock() - start) * 1000.0 / float(repeats))
        sample = sample + 1
    }
    let report = {
        words:len(tokens(paragraph)^),unknown:missing,checksum:checksum,
        warmup_paragraphs:5,paragraphs_per_sample:repeats,samples_ms:samples,
        first_paragraph_ms:(first_end - first_start) * 1000.0,
        read_ms:(read_end - read_start) * 1000.0,
        index_ms:(load_end - read_end) * 1000.0,
        load_ms:(load_end - read_start) * 1000.0,
        dictionary_bytes:len(dictionary.bytes),offsets:len(dictionary.offsets),
        offset_payload_bytes:len(dictionary.offsets) * 4,cache:false
    }
    output(report,"temp/spelling_vocab_poc/front/benchmark.json",{format:"json"})^;
    [report.words == 100,missing == ["teh","recieve","speling"],checksum == (5 + 9 * repeats) * 3]
}
