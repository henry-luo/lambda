// iCalendar parser test with simple calendar data
// Tests basic ICS parsing and field extraction

"\n=== Testing simple iCalendar parsing ==="

let simple_calendar = input('./test/input/simple.ics', 'ics')

"Simple iCalendar result:"
simple_calendar

"\n=== Testing calendar properties ==="

"Version: " + simple_calendar.version
"Product ID: " + simple_calendar.product_id
"Calendar Scale: " + simple_calendar.calendar_scale
"Method: " + simple_calendar.method

"\n=== Testing components ==="
"Number of components: " + simple_calendar.components.length
"First component type: " + simple_calendar.components[0].type

"\n=== Testing event details ==="
let first_event = simple_calendar.components[0]
"Event Summary: " + first_event.summary
"Event Description: " + first_event.description
"Event Location: " + first_event.location
"Event Status: " + first_event.status
"Event Priority: " + first_event.priority
"Event UID: " + first_event.uid
"Event Organizer: " + first_event.organizer
"Event Attendee: " + first_event.attendee

"\n=== Testing structured start time ==="
"Start Time structure:"
first_event.start_time

if (type(first_event.start_time) == "map") {
    "Start Year: " + first_event.start_time.year
    "Start Month: " + first_event.start_time.month  
    "Start Day: " + first_event.start_time.day
    "Start Hour: " + first_event.start_time.hour
    "Start Minute: " + first_event.start_time.minute
    "Start Second: " + first_event.start_time.second
    "Timezone: " + first_event.start_time.timezone
}

"\n=== Testing structured end time ==="
"End Time structure:"
first_event.end_time

"\n=== Testing raw properties access ==="
"All raw properties:"
first_event.properties

"\n=== Testing format conversions ==="
"JSON format:"
format(simple_calendar, 'json')

"XML format:"
format(simple_calendar, 'xml')

"YAML format:"
format(simple_calendar, 'yaml')

"Simple iCalendar test completed successfully!"
