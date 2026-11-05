#!/usr/bin/env python3
"""Rebuild the small, deterministic offline DBench sample database."""
from pathlib import Path
import sqlite3

path = Path(__file__).with_name("sample.sqlite")
path.unlink(missing_ok=True)
with sqlite3.connect(path) as db:
    db.executescript("""
        PRAGMA foreign_keys=ON;
        CREATE TABLE customers (
            customer_id INTEGER PRIMARY KEY, name TEXT NOT NULL, email TEXT UNIQUE,
            city TEXT NOT NULL, joined_on TEXT NOT NULL, active INTEGER NOT NULL, note TEXT
        );
        CREATE TABLE products (
            product_id INTEGER PRIMARY KEY, sku TEXT NOT NULL UNIQUE,
            name TEXT NOT NULL, price NUMERIC NOT NULL, picture BLOB
        );
        CREATE TABLE orders (
            order_id INTEGER PRIMARY KEY, customer_id INTEGER NOT NULL REFERENCES customers,
            product_id INTEGER NOT NULL REFERENCES products, placed_on TEXT NOT NULL,
            quantity INTEGER NOT NULL, status TEXT NOT NULL
        );
        CREATE INDEX orders_customer ON orders(customer_id);
        CREATE INDEX orders_product ON orders(product_id);
        CREATE VIEW order_summary AS
            SELECT o.order_id, c.name AS customer, p.name AS product, o.quantity,
                   p.price, o.status, o.placed_on
            FROM orders o JOIN customers c ON c.customer_id=o.customer_id
            JOIN products p ON p.product_id=o.product_id;
    """)
    cities = ("Kuala Lumpur", "Penang", "Singapore", "Melbourne", "London", "Zürich")
    db.executemany("INSERT INTO customers VALUES (?, ?, ?, ?, ?, ?, ?)", (
        (n, f"Customer {n:03}", f"customer{n:03}@example.test", cities[n % len(cities)],
         f"2024-{1 + n % 12:02}-{1 + n % 28:02}", n % 9 != 0,
         None if n % 5 else "Preferred customer") for n in range(1, 301)))
    db.executemany("INSERT INTO products VALUES (?, ?, ?, ?, ?)", (
        (n, f"P-{n:04}", f"Product {n:03}", f"{5 + n % 75}.{n % 100:02}",
         b"\x00\x01\xff" if n % 7 == 0 else None) for n in range(1, 81)))
    states = ("new", "packed", "shipped", "complete")
    db.executemany("INSERT INTO orders VALUES (?, ?, ?, ?, ?, ?)", (
        (n, 1 + n % 300, 1 + n % 80, f"2025-{1 + n % 12:02}-{1 + n % 28:02}",
         1 + n % 5, states[n % len(states)]) for n in range(1, 12001)))
    fields = [f"field_{n:03}" for n in range(1, 81)]
    db.execute("CREATE TABLE wide_records (record_id INTEGER PRIMARY KEY, " +
               ", ".join(f'"{name}" TEXT' for name in fields) + ")")
    db.executemany("INSERT INTO wide_records VALUES (" + ",".join("?" for _ in range(81)) + ")", (
        (n, f"Start {n}", *([None] * 78), f"End {n}") for n in range(1, 401)))
