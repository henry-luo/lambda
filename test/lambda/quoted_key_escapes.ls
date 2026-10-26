// S8.2.2v6: quoted names and value keys share their decoded spelling.
let names = {'\\':1,'\n':2,'\'':3,'\u4E2D':4,'\u{1F600}':5,'\uD83D\uDE01':6}
let node = <record '\\':7, '\u4E2D':8>
let plain = {field:9};
[
    names["\\"] == 1,
    names['\\'] == 1,
    names["\n"] == 2,
    names["'"] == 3,
    names["中"] == 4,
    names['中'] == 4,
    names["😀"] == 5,
    names["😁"] == 6,
    names["\\\\"] == null,
    names["\\u4E2D"] == null,
    node["\\"] == 7,
    node["中"] == 8,
    plain.field == 9
]
