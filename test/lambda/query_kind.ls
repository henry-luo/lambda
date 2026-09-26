// S8.2.4v3: a type subscript is an accessor -- `e[T]` and `e?T` extend `e[1]`
// the way a name extends a position -- so a query yields the run `T*` a
// subscript yields (S2.5.5v2): null for no match, the match itself for one,
// a list for two or more. It is a value, not an item-position producer.
// Golden written from the ruling, not from the runtime.
let html = <div <img src: "a"> <p "hi">>
let two = <div <img src: "a"> <img src: "b">>?<img>;
"-- one match is the match itself --";
[type(html?<img>), html?<img> == html[0], html?<img>];
"-- no match is null, which reads as absence --";
[html?<table> == null, if (html?<table>) "found" else "absent"];
"-- two or more is a list --";
[type(two), len(two)];
[two, 9];
"-- the child query follows; a list is stepped item by item, and a scalar item has no content --";
[type(html[element]), html[int] == null, [1, "a"][int], type((1, "a", 2)[int])];
"-- a value, never a producer: absence lands as null; the packager splices --";
[html?<table>, 9];
[*html?<table>, 9];
"-- the match count is count(q) (S8.3.3v3) --";
[count(html?<img>), count(html?<table>), count(two)];
"-- the result type is T* --";
[html?<img> is element*, html?<table> is element*, two is element*, html?<img> is element[]]
