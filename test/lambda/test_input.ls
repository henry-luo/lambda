let csv =input("test/input/test.csv", 'csv')
csv
for row in csv {
  if (row.age > 18) {
    row.name +" of age "+ row.age
  }
}