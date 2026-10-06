// RDB corpus: column values decode to the same Lambda types on every backend (RDB6)
let db = input("{{RDB_URI}}")^
let b = db.data.books[0]
let a = db.data.authors[0];

type(b.id);
b.id;
type(b.price);
b.price;
type(b.published);
b.published;
type(b.metadata);
b.metadata.pages;
b.metadata.tags;
type(db.data.books[2].metadata);
type(a.rating);
a.rating;
type(a.active);
db.data.authors[2].active;
type(a.born);
a.born;
type(b.cover)
