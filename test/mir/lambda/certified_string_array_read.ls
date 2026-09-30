// D3.3.3v3: a declared pointer-lane array can be read through its certificate.
let letters: string[] = ["A", "B", "C"]

fn pick(index: int) string? => letters[index]
fn compound(index: int) string? => letters[(index % 2) + shr(index, 1)]
fn fallible() string? => letters[shr(1, -1) + 1]

let unproved: any = letters;
[pick(1), pick(3), compound(2), fallible(), unproved[2], letters[0]]
