let csv =input("test/input/test.csv", 'csv')?
csv[1]
"Seniors:"
for row in csv {
  if (int(row.age) >= 50) {
    '-'; row.name ++ " of age " ++ row.age
  }
}
