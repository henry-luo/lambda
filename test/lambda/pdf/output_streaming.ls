import radiant

fn rows(n) => for (i in range(0, n, 1)) (string(i), ":")
fn nested(n) => if (n == 0) "empty" else ("[", rows(n), "]")
fn typed(n: int) string => string(n)
fn array_result() => ["a", "b"]
fn broken() any^ => ("discarded", error("failed"))
fn recovered() any^ => (broken() ^ {"recovered"})
fn recovered_payload(n) any^ => (broken() ^ {let allocated = string(n); ^.message ++ allocated})
fn make_capture(prefix) => (n) => prefix ++ string(n)

let zero = <file format:'pdf', for (i in range(0, 0, 1)) string(i)>;
let one = <file format:'pdf', rows(1)>;
let many = <file format:'pdf', "prefix" nested(3) typed(4) "suffix">;
let recovered_file = <file format:'pdf', "before" recovered()^ "after">;
let encode_rollback = <file format:'pdf', ("before", (("discarded", 42) ^ {"replacement"}), "after")>;
let windowed = <file format:'pdf', for (i in [3, 1, 2] order by i limit 2) string(i)>;
let saved_array = ["a", "b"];
let ordinary_list = rows(2);
let closure = make_capture("C");
let dynamic_format = 'pdf';
let ordinary_format = 'other';
[binary(zero) == binary(""), binary(one) == binary("0:"),
    binary(many) == binary("prefix[0:1:2:]4suffix"),
    binary(recovered_file) == binary("beforerecoveredafter"),
    binary(encode_rollback) == binary("beforereplacementafter"),
    binary(windowed) == binary("12"),
    (<file format:'pdf', saved_array> ^ {true}) == true,
    binary(<file format:'pdf', closure(5)>) == binary("C5"),
    binary(<file format:dynamic_format, rows(2)>) == binary("0:1:"),
    content(<file format:ordinary_format, rows(2)>) == ["0:1:"],
    radiant.pdf_measure({Text: "a(b)\\\n\t", Unicode: "λ😀", Name: 'A B', Bytes: binary("x"),
        Number: 1e-12, Values: [true, null, 12]})^ ==
        len(binary(<file format:'pdf', {Text: "a(b)\\\n\t", Unicode: "λ😀", Name: 'A B', Bytes: binary("x"),
            Number: 1e-12, Values: [true, null, 12]}>)),
    (<file format:'pdf', array_result()> ^ {true}) == true,
    ordinary_list == ("0", ":", "1", ":"),
    binary(<file format:'pdf', recovered_payload(17)^>) == binary("failed17")]
