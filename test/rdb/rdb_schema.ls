// RDB corpus: schema introspection; declared types and index names are
// backend-specific, so this script has per-backend goldens
let db = input("{{RDB_URI}}")^;

[for (c in db.schema.books.columns) c.name ++ (if (c.pk) "*" else "") ++ (if (c.nullable) "?" else "")];
[for (c in db.schema.books.columns) c.type];
[for (f in db.schema.books.foreign_keys) f.column ++ " -> " ++ f.ref_table ++ "." ++ f.ref_column];
[for (i in db.schema.books.indexes) i.name ++ (if (i.unique) "!" else "")];
[for (t in db.schema.books.triggers) t.name ++ ": " ++ t.timing ++ " " ++ t.event];
len(db.schema.authors.columns)
