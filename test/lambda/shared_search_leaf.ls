[
    contains("abcabc", "bc"),
    contains("a\u0000bc", "\u0000b"),
    contains("éxé", "é"),
    contains("😀x😀", "😀"),
    contains("", ""),
    contains("abc", ""),
    contains("", "x"),
    contains("abc", "longer"),
    contains("abc", "xy"),
    index_of("😀x😀", "😀"),
    last_index_of("😀x😀", "😀"),
    index_of("a\u0000bc", "\u0000b"),
    last_index_of("abcabc", "bc"),
    index_of("abc", "longer")
]
