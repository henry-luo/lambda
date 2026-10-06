-- RDB driver corpus fixture (vibe/Lambda_IO_RDB.md section 13.11); reloaded by utils/rdb-test-servers.sh
CREATE TABLE authors (id INT PRIMARY KEY, name VARCHAR(100) NOT NULL, born DATE, rating DOUBLE DEFAULT 0.0, active BOOLEAN DEFAULT TRUE);
CREATE TABLE books (id INT PRIMARY KEY, title VARCHAR(200) NOT NULL UNIQUE, author_id INT NOT NULL, price DECIMAL(8,2), published DATETIME, metadata JSON, cover BLOB, FOREIGN KEY (author_id) REFERENCES authors(id));
CREATE VIEW active_authors AS SELECT * FROM authors WHERE active;
CREATE TRIGGER trg_books_before_insert BEFORE INSERT ON books FOR EACH ROW SET NEW.title = NEW.title;
INSERT INTO authors VALUES (1,'Alice','1985-03-15',4.8,TRUE),(2,'Bob','1990-07-22',3.5,TRUE),(3,'Charlie','1978-11-01',4.2,FALSE);
INSERT INTO books (id,title,author_id,price,published,metadata) VALUES
 (1,'Lambda Calculus',1,29.99,'2023-01-15 10:30:00','{"tags":["math","cs"],"pages":320}'),
 (2,'Type Theory',1,39.99,'2023-06-20 14:00:00','{"tags":["logic"],"pages":450}'),
 (3,'Functional Programming',2,24.50,'2024-02-10 09:00:00',NULL);
