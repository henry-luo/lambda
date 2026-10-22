import vocab: lambda.doc.spelling.vocabulary

fn rejected(data) => (vocab.from_data(data) ^ { ^.code }) == 7820

pn main() {
    let words = vocab.read("test/input/spelling/vocab_array.json")^
    let empty = vocab.from_data({format_version:1,language:"en-US",keys:[],entries:[]})^;
    [
        len(words.entries) == 6,
        vocab.lookup(words,"cat")^ == ["cat","ordinary"],
        vocab.lookup(words,"cafe\u0301")^[0] == "café",
        vocab.lookup(words,"Lambda")^[1] == "exact",
        vocab.lookup(words,"lambda")^ == null,
        vocab.lookup(words,"dogs")^[0] == "dogs",
        vocab.lookup(words,"aardvark")^ == null,
        vocab.lookup(words,"zebra")^ == null,
        vocab.lookup(empty,"cat")^ == null,
        rejected({format_version:2,language:"en-US",keys:[],entries:[]}),
        rejected({format_version:1,language:"en-US",keys:["cat"],entries:[]}),
        rejected({format_version:1,language:"en-US",keys:["cat"],entries:[["dog","ordinary"]]}),
        rejected({format_version:1,language:"en-US",keys:["dog","cat"],entries:[["dog","ordinary"],["cat","ordinary"]]}),
        rejected({format_version:1,language:"en-US",keys:["cat","cat"],entries:[["cat","ordinary"],["cat","ordinary"]]}),
        rejected({format_version:1,language:"en-US",keys:["cat"],entries:[["cat","ordinary",-1]]}),
        rejected({format_version:1,language:"en-US",keys:["cat"],entries:[["cat","ordinary",null,4]]})
    ]
}
