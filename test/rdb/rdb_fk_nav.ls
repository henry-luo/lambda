// RDB corpus: forward and reverse foreign-key navigation (§2.5)
let db = input("{{RDB_URI}}")^;

db.data.books[0].author.name;
[for (b in db.data.books) b.author.name];
[for (a in db.data.authors) len(a.books)];
[for (b in db.data.authors[0].books) b.title];
db.schema.books.foreign_keys[0].link_name
