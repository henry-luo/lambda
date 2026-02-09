// Test CSV parsing with headers (comma-separated)
let csv_with_headers = input('./test/input/test.csv', 'csv')?
"CSV with headers (comma-separated):"
csv_with_headers

// Test CSV parsing with tab separator
let csv_tab_separated = input('./test/input/test_tab.csv', 'csv')?
"\nCSV with tab separator:"
csv_tab_separated

// Test CSV parsing without headers
let csv_no_headers = input('./test/input/test_no_header.csv', 'csv')?
"\nCSV without headers (should be array of arrays):"
csv_no_headers

// Test accessing map data
"\nTesting map access - first person's name:"
let headers = csv_with_headers[0]
headers  // .name

"\nTesting map access - first person's age:"
headers   // .age

"\nTesting array access - first row, first column:"
let first_row = csv_with_headers[0]
first_row[0]
