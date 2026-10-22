import front: lambda.doc.spelling.front_coded

fn rejected(value, count = null) => (front.from_bytes(value,count) ^ { ^.code }) == 7821
fn word_at(i) => "word" ++ slice("0000" ++ string(i),len(string(i)))
fn matches(text, words, missing) bool^ {
    let dictionary = front.from_bytes(binary(text),len(words))^;
    all([for (i,word in words) front.lookup(dictionary,word)^ == i]) and
        all([for (word in missing) front.lookup(dictionary,word)^ == null])
}

pn main() {
    let raw = binary("catalog;ue;5,yst|dog;s\n")
    let small = front.from_bytes(raw,5)^
    let unicode = front.from_bytes(binary("élan;s;2,égant\n"))^
    let blocks = front.read("test/input/spelling/vocab_front.dict",130)^
    let first_line = slice(blocks.bytes,0,int(blocks.offsets[1]))^
    let digits = join([for (i in 1 to 80) "7"],"");
    [
        small.bytes == raw and small.count == 5 and len(small.offsets) == 1,
        small.offsets is u32[],
        front.lookup(small,"catalog")^ == 0,
        front.lookup(small,"catalogue")^ == 1,
        front.lookup(small,"catalyst")^ == 2,
        front.lookup(small,"dog")^ == 3,
        front.lookup(small,"dogs")^ == 4,
        front.lookup(small,"cat")^ == null,
        front.lookup(small,"catalogues")^ == null,
        front.lookup(small,"zoo")^ == null,
        front.lookup(small,"")^ == null,
        front.lookup(small,"Catalog")^ == null,
        front.lookup(unicode,"e\u0301lan")^ == 0,
        front.lookup(unicode,"élans")^ == 1,
        front.lookup(unicode,"élégant")^ == 2,
        len(blocks.offsets) == 3 and blocks.count == 130,
        all([for (i in 0 to 129) front.lookup(blocks,word_at(i))^ == i]),
        front.lookup(blocks,"word0063x")^ == null,
        front.lookup(blocks,"word0129z")^ == null,
        rejected(null),
        rejected(binary("cat")),
        rejected(binary("cat\n"),2),
        rejected(binary("cat\ncat\n")),
        rejected(binary("cat;0,s\n")),
        rejected(binary("cat;4,s\n")),
        rejected(binary("cat;01,s\n")),
        rejected(binary("cat;x,s\n")),
        rejected(binary("cat;2,\n")),
        rejected(binary("cat;\n")),
        rejected(binary("cat|cat\n")),
        rejected(binary("dog|cat\n")),
        rejected(binary("cat,s\n")),
        rejected(binary("cat\r\n")),
        rejected(b'\xFF0A'),
        rejected(binary("cafe\u0301\n")),
        rejected(binary("cat\u0000\n")),
        rejected(binary("\uFEFFcat\n")),
        rejected(binary(join([for (i in 0 to 64) word_at(i)],"|") ++ "\n")),
        rejected(first_line ++ first_line),
        rejected(binary("cat;3,s,t\n")),
        // retained mismatches must be skipped until a prefix retreat or literal reset.
        matches("car;d;s;3,e;1,om;3,b|dog;2,t|end\n",
            ["car","card","cards","care","com","comb","dog","dot","end"],
            ["cas","cat","catz","comba","doo","e"])^,
        // a shared UTF-8 lead byte is not a shared complete code point.
        matches("aè;z;1,é;x;2,z;1,ê|b\n",
            ["aè","aèz","aé","aéx","aéz","aê","b"],
            ["aéw","aéy","aë","aèzz"])^,
        matches("a😀;a;2,b;1,😁|b\n",
            ["a😀","a😀a","a😀b","a😁","b"],
            ["a😀aa","a😀c","a😂"])^,
        matches("aaa;b;3,c;1,b\n",["aaa","aaab","aaac","ab"],["aab","ac"])^,
        // leading digits without a comma belong to an ordinary appended suffix.
        matches("foo;123;4,4;3,2;3,23\n",
            ["foo","foo123","foo14","foo2","foo23"],["foo13","foo22","foo3"])^,
        matches("1;2;1,3|a\n",["1","12","13","a"],["11","123","14"])^,
        matches("foo;" ++ digits ++ ";3,z\n",["foo","foo" ++ digits,"fooz"],
            ["foo" ++ digits ++ "0","foozz"])^,
        matches("abcdefghij;a;10,b;10,c\n",
            ["abcdefghij","abcdefghija","abcdefghijb","abcdefghijc"],
            ["abcdefgh","abcdefghijaa","abcdefghijd"])^
    ]
}
