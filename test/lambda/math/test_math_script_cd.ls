// D10.2.2v2: CD arrows retain labels and their matrix separators.
let tree = parse("\\begin{CD}A@<a<<B@>>b>C\\\\@|@AcAA@VVdV\\\\D@=E@>>>F\\end{CD}",'math')^;
let arrows = [for (item in tree[0].body where item is element and (name(item) ^ { null }) == 'cd_arrow') item];
[
    len(arrows) == 7,
    arrows[0].direction == "<" and arrows[0].source == "@<a<<",
    arrows[1].direction == ">" and arrows[1].source == "@>>b>",
    arrows[2].direction == "|" and arrows[2].source == "@|",
    arrows[3].direction == "A" and arrows[3].source == "@AcAA",
    arrows[4].direction == "V" and arrows[4].source == "@VVdV",
    arrows[5].direction == "=" and arrows[5].source == "@=",
    arrows[6].direction == ">" and arrows[6].source == "@>>>"
]
