// Lambda Script HTTP/HTTPS Input Examples
// Demonstrates the new HTTP/HTTPS support in the input system

// Example 1: Download and parse JSON data from an API
let github_zen = input("https://api.github.com/zen", 'auto')
github_zen

// Example 2: Download and parse structured JSON data
let json_data = input("https://httpbin.org/json", 'json')
json_data

// Example 3: Download CSV data from a remote source
// Note: This would work with any CSV endpoint
// let csv_data = input("https://example.com/data.csv", 'csv')

// Example 4: Download XML data
// let xml_data = input("https://httpbin.org/xml", 'xml')

// Example 5: Download HTML and parse it
// let html_data = input("https://httpbin.org/html", 'html')

// Example 6: Download plain text
let user_agent = input("https://httpbin.org/user-agent", 'auto')
user_agent

// Example 7: Download and auto-detect format
let auto_detected = input("https://httpbin.org/json", 'auto')
auto_detected

// Example 8: Download with explicit type specification
let explicit_json = input("https://httpbin.org/json", 'json')
explicit_json
