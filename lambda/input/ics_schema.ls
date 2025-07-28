// ICS Schema Definition in Lambda Script
// Comprehensive schema for iCalendar format (RFC 5545)
// Defines structure for calendar data including events, todos, and journals

// Basic data types for calendar
type DateTime = string
type Date = string
type Duration = string
type UTCOffset = string
type URI = string
type CalAddress = string
type Text = string
type Integer = int
type Float = float

// Calendar component properties
type CommonProperties = {
    uid: string?,
    dtstamp: DateTime?,
    'created': DateTime?,
    'last-modified': DateTime?,
    sequence: Integer?,
    summary: Text?,
    description: Text?,
    comment: [Text*]?,
    categories: [Text*]?,
    'class': 'PUBLIC' | 'PRIVATE' | 'CONFIDENTIAL'?,
    status: string?,
    url: URI?,
    'attach': [URI*]?,
    'contact': [Text*]?,
    'related-to': [string*]?
}

// Event-specific properties
type EventProperties = CommonProperties & {
    dtstart: DateTime?,
    dtend: DateTime?,
    duration: Duration?,
    location: Text?,
    'geo': string?,  // latitude;longitude
    organizer: CalAddress?,
    attendee: [CalAddress*]?,
    'transparency': 'OPAQUE' | 'TRANSPARENT'?,
    'priority': Integer?,  // 0-9
    'resources': [Text*]?,
    'rrule': string?,  // Recurrence rule
    'rdate': [DateTime*]?,
    'exdate': [DateTime*]?,
    'exrule': [string*]?
}

// Todo-specific properties  
type TodoProperties = CommonProperties & {
    dtstart: DateTime?,
    due: DateTime?,
    duration: Duration?,
    completed: DateTime?,
    'percent-complete': Integer?,  // 0-100
    priority: Integer?,  // 0-9
    resources: [Text*]?,
    'rrule': string?,
    'rdate': [DateTime*]?,
    'exdate': [DateTime*]?
}

// Journal-specific properties
type JournalProperties = CommonProperties & {
    dtstart: DateTime?,
    'rrule': string?,
    'rdate': [DateTime*]?,
    'exdate': [DateTime*]?
}

// Freebusy-specific properties
type FreebusyProperties = {
    uid: string?,
    dtstamp: DateTime?,
    organizer: CalAddress?,
    attendee: [CalAddress*]?,
    dtstart: DateTime?,
    dtend: DateTime?,
    freebusy: [string*]?,
    url: URI?,
    comment: [Text*]?,
    contact: [Text*]?
}

// Timezone component properties
type TimezoneProperties = {
    tzid: string,
    'tzurl': URI?,
    'last-modified': DateTime?
}

type StandardProperties = {
    dtstart: DateTime,
    tzoffsetfrom: UTCOffset,
    tzoffsetto: UTCOffset,
    tzname: [Text*]?,
    comment: [Text*]?,
    'rdate': [DateTime*]?,
    'rrule': string?
}

type DaylightProperties = StandardProperties

// Alarm component properties
type AlarmProperties = {
    action: 'AUDIO' | 'DISPLAY' | 'EMAIL' | 'PROCEDURE',
    trigger: string,  // Duration or DateTime
    repeat: Integer?,
    duration: Duration?,
    
    // Action-specific properties
    description: Text?,  // For DISPLAY and EMAIL
    summary: Text?,      // For EMAIL
    attendee: [CalAddress*]?,  // For EMAIL
    'attach': URI?       // For AUDIO and PROCEDURE
}

// Calendar components
type VEvent = {
    component: 'VEVENT',
    properties: EventProperties,
    alarms: [VAlarm*]?
}

type VTodo = {
    component: 'VTODO', 
    properties: TodoProperties,
    alarms: [VAlarm*]?
}

type VJournal = {
    component: 'VJOURNAL',
    properties: JournalProperties
}

type VFreebusy = {
    component: 'VFREEBUSY',
    properties: FreebusyProperties
}

type VTimezone = {
    component: 'VTIMEZONE',
    properties: TimezoneProperties,
    standard: [VStandard*]?,
    daylight: [VDaylight*]?
}

type VStandard = {
    component: 'STANDARD',
    properties: StandardProperties
}

type VDaylight = {
    component: 'DAYLIGHT', 
    properties: DaylightProperties
}

type VAlarm = {
    component: 'VALARM',
    properties: AlarmProperties
}

// Calendar properties
type CalendarProperties = {
    prodid: string,  // Required
    version: string, // Required, should be "2.0"
    calscale: 'GREGORIAN'?,
    method: 'PUBLISH' | 'REQUEST' | 'REPLY' | 'ADD' | 'CANCEL' | 'REFRESH' | 'COUNTER' | 'DECLINECOUNTER'?,
    'x-wr-calname': Text?,
    'x-wr-caldesc': Text?,
    'x-wr-timezone': string?,
    'x-*': Text?  // Custom properties
}

// Main calendar structure
type VCalendar = {
    component: 'VCALENDAR',
    properties: CalendarProperties,
    components: [CalendarComponent*]
}

type CalendarComponent = VEvent | VTodo | VJournal | VFreebusy | VTimezone

// Validation rules for iCalendar
type ICSValidationRules = {
    // Required calendar properties
    required_calendar_properties: ['prodid', 'version'],
    
    // Required version value
    required_version: '2.0',
    
    // Valid component names
    valid_components: ['VCALENDAR', 'VEVENT', 'VTODO', 'VJOURNAL', 'VFREEBUSY', 'VTIMEZONE', 'VALARM', 'STANDARD', 'DAYLIGHT'],
    
    // DateTime format pattern (simplified)
    datetime_pattern: '^[0-9]{8}T[0-9]{6}Z?$',
    
    // Priority range
    priority_range: { min: 0, max: 9 },
    
    // Percent complete range
    percent_complete_range: { min: 0, max: 100 }
}

// Root document type for ICS validation
type ICSDocument = VCalendar
