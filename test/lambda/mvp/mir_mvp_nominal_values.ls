// S16.9.5/S2.1.3: optional presence and defaults precede nominal construction;
// content keeps element normalization and the declared structural kind.
type Entry { value: float, note?: string, rank: int = 3 }
type Page { title: string, string* }
fn text(x) => x ++ "!"
let original = <Entry value: 1.25>
let updated = <Entry *: original, value: 2.5, note: "ready">
let page = <Page title: text("page"), text("a") null text("b")>;
[
    original.value == 1.25 and original.rank == 3,
    not ('note' at original),
    updated.value == 2.5 and updated.note == "ready" and updated.rank == 3,
    page is Page and page is object and page is element,
    page.title == "page!" and page[0] == "a!b!",
    len(page) == 2
]
