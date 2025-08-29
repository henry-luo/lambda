let csv =input("test/input/test.csv", 'csv')
csv
"Seniors:"
for row in csv {
  int(row.age)
  if (int(row.age) >= 50) {
    row.name +" of age "+ row.age
  }
}