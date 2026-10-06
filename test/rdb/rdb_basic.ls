// RDB corpus: tables, views, row counts and projections — identical on every backend
let db = input("{{RDB_URI}}")^;

db.table_names;
db.table_count;
len(db.data.authors);
len(db.data.books);
[for (b in db.data.books) b.title];
sort([for (a in db.data.active_authors) a.name]);
db.schema.active_authors.view;
db.schema.books.view == true
