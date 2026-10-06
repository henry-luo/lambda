-- RDB driver corpus fixture (vibe/Lambda_IO_RDB.md section 13.11); reloaded by utils/rdb-test-servers.sh
CREATE TABLE authors (id INTEGER PRIMARY KEY, name TEXT NOT NULL, born DATE, rating DOUBLE PRECISION DEFAULT 0.0, active BOOLEAN DEFAULT true);
CREATE TABLE books (id INTEGER PRIMARY KEY, title TEXT NOT NULL UNIQUE, author_id INTEGER NOT NULL REFERENCES authors(id), price NUMERIC(8,2), published TIMESTAMP, metadata JSONB, cover BYTEA);
CREATE INDEX idx_books_author ON books(author_id);
CREATE VIEW active_authors AS SELECT * FROM authors WHERE active;
CREATE FUNCTION noop_trigger() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN RETURN NEW; END $$;
CREATE TRIGGER trg_books_before_insert BEFORE INSERT ON books FOR EACH ROW EXECUTE FUNCTION noop_trigger();
INSERT INTO authors VALUES (1,'Alice','1985-03-15',4.8,true),(2,'Bob','1990-07-22',3.5,true),(3,'Charlie','1978-11-01',4.2,false);
INSERT INTO books (id,title,author_id,price,published,metadata) VALUES
 (1,'Lambda Calculus',1,29.99,'2023-01-15 10:30:00','{"tags":["math","cs"],"pages":320}'),
 (2,'Type Theory',1,39.99,'2023-06-20 14:00:00','{"tags":["logic"],"pages":450}'),
 (3,'Functional Programming',2,24.50,'2024-02-10 09:00:00',NULL);
