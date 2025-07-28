// Simple iCalendar parser test with complex calendar data
// Tests multiple components

"\n=== Testing complex iCalendar parsing ==="

let complex_calendar = input('./test/input/calendar.ics', 'ics')

"Complex iCalendar result:"
complex_calendar

"\n=== Testing calendar-level properties ==="
"Version: " + complex_calendar.version
"Product ID: " + complex_calendar.product_id
"Calendar Scale: " + complex_calendar.calendar_scale
"Method: " + complex_calendar.method

"\n=== Testing multiple components ==="
"Total components: " + complex_calendar.components.length

// Access individual components
let component0 = complex_calendar.components[0]
let component1 = complex_calendar.components[1]  
let component2 = complex_calendar.components[2]

"Component 1 type: " + component0.type
"Component 2 type: " + component1.type
"Component 3 type: " + component2.type

"\n=== Testing first event details ==="
let first_event = complex_calendar.components[0]
"Event Summary: " + first_event.summary
"Event Description: " + first_event.description
"Event Location: " + first_event.location
"Event Status: " + first_event.status
"Event Priority: " + first_event.priority

"\n=== Testing first todo details ==="
let first_todo = complex_calendar.components[1]
"Todo Summary: " + first_todo.summary
"Todo Description: " + first_todo.description
"Todo Status: " + first_todo.status
"Todo Priority: " + first_todo.priority

"\n=== Testing third event details ==="
let third_event = complex_calendar.components[2]
"Event with duration - Summary: " + third_event.summary
"Duration structure:"
third_event.duration

"\n=== Testing structured datetime parsing ==="
"First event start time structure:"
first_event.start_time

"\n=== Testing component validation ==="
"First component is event: " + (first_event.type == "VEVENT")
"Second component is todo: " + (component1.type == "VTODO")
"Components have properties: " + (type(first_event.properties) == "map")

"\n=== Testing format conversions ==="
"JSON format:"
format(complex_calendar, 'json')
"TOML format:"
format(complex_calendar, 'toml')

"All iCalendar tests completed successfully!"
