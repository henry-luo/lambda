// explicit full-pack qualification, separate from the always-available synthetic goldens.
import ime: lambda.ime.chinese
import spelling: lambda.ime.chinese.syllables

fn position(items,text) => index_of([for (item in items) item.text],text)
pn query(engine,text,word,fuzzy,options) map^ {
    output({phase:"query",input:text,word:word,fuzzy:fuzzy},
        "temp/ime_chinese/m1/progress.json",{format:"json"})^
    let samples = [for (i in 1 to 3) {
        let started = clock()
        let result = ime.candidates(engine,text,{*:options,fuzzy:fuzzy})^;
        {result:result,ms:(clock() - started) * 1000.0}
    }]
    let result = samples[0].result
    let elapsed = sort([for (sample in samples) sample.ms])[1]
    let at = position(result.items,word);
    return {input:text,rank:if (at == null) null else at + 1,truncated:result.truncated,
        keys:result.scanned.keys,postings:result.scanned.postings,ms:elapsed,
        samples_ms:[for (sample in samples) sample.ms],
        weight:if (at == null) null else result.items[at].weight,
        first:if (len(result.items) == 0) null else result.items[0].text}
}

pn main() {
    let fixture = input("test/ime/heldout.json","json")^
    let options = if (exists("temp/ime_chinese/m1/eval-options.json"))
        input("temp/ime_chinese/m1/eval-options.json","json")^ else {}
    let started = clock()
    let engine = ime.load("zh-Hans")^
    let loaded = clock()
    output({phase:"loaded",load_ms:(loaded - started) * 1000.0},
        "temp/ime_chinese/m1/progress.json",{format:"json"})^
    let rows = [for (case in fixture.cases) {
        let syllables = split(case[1],"'")
        let full = join(syllables,"")
        let initials = join([for (syllable in syllables) {
            let head = spelling.initial(string(syllable));
            if (head == "") syllable[0] else head
        }],"");
        let row = {word:case[0],reading:case[1],full:query(engine,full,case[0],"none",options)^,
            initials:query(engine,initials,case[0],"none",options)^,
            fuzzy:query(engine,full,case[0],"all",options)^};
        row
    }]
    let report = {load_ms:(loaded - started) * 1000.0,options:options,
        bytes:len(engine.lexicon.bytes) + len(engine.abbrev.bytes),
        offset_payload_bytes:4 * (len(engine.lexicon.offsets) + len(engine.abbrev.offsets)),rows:rows};
    output(report,"temp/ime_chinese/m1/evaluation.json",{format:"json"})^;
    [len(rows) == len(fixture.cases)]
}
