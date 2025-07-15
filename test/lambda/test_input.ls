let csv = input("test/input/test.csv", 'csv')
(csv)
({a: 1, b: 2, c: 3}.b)
(<e a:"str", b:2>.a)
<e a:123,
b:456, c:789>
for row in csv {
  if (row.age > 18) {
    row.name +" of age "+ row.age
  }
}