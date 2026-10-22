// positional vocabulary POC; IO is procedural (S12.1.1v3), data uses source-package paths (D7.2.4v3).
import search: lambda.doc.diff.common

fn valid_row(row, key) => row is array and len(row) >= 2 and len(row) <= 4 and
    row[0] is string and normalize(row[0],"nfc") == key and
    row[1] in ["ordinary","exact"] and
    (len(row) < 3 or row[2] == null or (row[2] is int and row[2] >= 0)) and
    (len(row) < 4 or (row[3] is int and row[3] >= 0 and row[3] <= 3))

// validate aligned IDs before lookup so a malformed key cannot return a different word.
pub fn from_data(data) map^ {
    if (not (data is map and data.format_version == 1 and data.language is string and
        data.keys is array and data.entries is array and len(data.keys) == len(data.entries)))
        raise error({code:7820,message:"spelling vocabulary: invalid header or array lengths"})
    else if (not all([for (i,key in data.keys) key is string and valid_row(data.entries[i],key) and
        (i == 0 or data.keys[i-1] < key)]))
        raise error({code:7820,message:"spelling vocabulary: invalid row, duplicate or unsorted key"})
    else data
}

// exact NFC lookup only; full spelling case policy and suggestions are outside this POC.
pub fn lookup(vocabulary, word: string) array?^ {
    let key = normalize(word,"nfc")
    let index = search.lower_bound(vocabulary.keys,key);
    if (index < len(vocabulary.keys) and vocabulary.keys[index] == key) vocabulary.entries[index]
    else null
}

pub pn read(path) map^ {
    let data = input(path,"json")^;
    return from_data(data)^
}
