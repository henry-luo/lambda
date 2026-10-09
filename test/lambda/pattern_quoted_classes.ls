// S11.1.2v4: quotes establish the domain and repeat as whole fragments.
type Digits = \("\d"+);
type Symbols = \('\a' '\w'*);
type SymbolCopy = \(Symbols);
type LiteralSymbols = 'if' | 'else';
type StringRun = "a"*;
type RepeatedText = \("a"*);
type d = \("\d");
type w = \('\w');
type a = "a" | "b";
type s = 'x' | 'y';
fn sym(value: Symbols) => value;
fn classify(value) => match value {
    case \('\d'+): "symbol"
    case \("\d"+): "string"
    default: "other"
};

'classes and domains';
["123" is Digits, not ("abc" is Digits), not ('123' is Digits),
 'ab_2' is Symbols, not ("ab_2" is Symbols), 'ab_2' is SymbolCopy,
 "1" is \("\d"), "x" is \("\w"), "\t" is \("\s"), "A" is \("\a"),
 '1' is \('\d'), 'x' is \('\w'), '\t' is \('\s'), 'A' is \('\a')];

'quoted fragments';
["a a " is \("a\s"*), not ("a  " is \("a\s"*)),
 "" is \("a\s"*), "1+" is \("\d+"), not ("12" is \("\d+")),
 "a1_x" is \("a\d\w" "x"), 'ID-1' is \('ID-\d'{1}),
 "dd" is \("d"+), 'aa' is \('a'+)];

'ordinary escapes are decoded once';
["\\d" is \("\\d"), not ("5" is \("\\d")),
 "\\d" is \("\u005cd"), "a" is \("\u0061"),
 "a" is \("\u{61}"), "😀" is \("\uD83D\uDE00"),
 "\n" is \("\n"), '"' is \('"'), "'" is \("'")];

'names, literals and character sets';
["7" is \(d), '7' is \(w), "ab" is \(a+), 'xy' is \(s+),
 'ifelse' is \(LiteralSymbols+), not ("ifelse" is \(LiteralSymbols+)),
 'x' is \(!'\d'), not ('5' is \(!'\d')),
 'q' is \('a' to 'z'), 'qq' is \(('a' to 'z')+),
 'c' is \(!('a' | 'b')), not ('a' is \(!('a' | 'b')))];

'wildcards inherit the whole island domain';
["x\nz" is \("" ...), 'x\nz' is \('x' ...),
 "ab" is \(. "b"), 'ab' is \(. 'b'), "x" is \("" | .),
 not ('x' is \("" ...)), not ("x" is \('x' ...))];

'contracts, operators and matching';
[sym('ab1') == 'ab1', classify('123') == "symbol",
 classify("123") == "string", classify('abc') == "other",
 ['ab1'] is Symbols[], {code: 'ab1'} is {code: Symbols},
 ("123" is (\("\d"+) | \('\d'+))),
 ('123' is (\("\d"+) | \('\d'+))),
 null is StringRun, ["a", "a"] is StringRun,
 not ("aa" is StringRun), "aa" is RepeatedText,
 not (null is RepeatedText)];

'search operations';
[replace("a1b22", Digits, "N") == "aNbN",
 split("a1b22c", Digits) == ["a", "b", "c"],
 (find("a1b22", Digits) |> ~.value) == ["1", "22"]]

'symbol literal unions cannot search strings';
[find("if", LiteralSymbols) is error,
 replace("if", LiteralSymbols, "x") is error,
 split("if", LiteralSymbols) is error]

'comments do not select domains or close islands';
["a1" is \(/* 'sym' ) */ "a" "\d"),
 'a1' is \('a' // "string" )
 '\d')]

'literal-only islands retain ordinary type identity';
type SymbolIsland = \('if' | 'else');
type SymbolGrouped = \(('if' | ('else')));
type LiteralStrings = "if" | "else";
type StringGrouped = \(("if" | ("else")));
[LiteralSymbols == SymbolIsland, LiteralSymbols == SymbolGrouped,
 LiteralStrings == StringGrouped]
