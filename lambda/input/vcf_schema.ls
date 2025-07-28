// VCF Schema Definition in Lambda Script  
// Comprehensive schema for vCard format (RFC 6350)
// Defines structure for contact information exchange

// Basic data types for vCard
type Text = string
type URI = string
type Date = string
type DateTime = string
type LanguageTag = string
type MediaType = string
type Integer = int
type Float = float

// Common vCard parameters
type CommonParameters = {
    language: LanguageTag?,
    pref: Integer?,      // Preference value 1-100
    pid: string?,        // Property ID
    type: [string*]?,    // Property type (HOME, WORK, etc.)
    value: 'text' | 'uri' | 'date' | 'time' | 'date-time' | 'date-and-or-time' | 'timestamp' | 'boolean' | 'integer' | 'float' | 'utc-offset' | 'language-tag'?,
    altid: string?,      // Alternative representation ID
    calscale: 'gregorian'?,
    sort-as: [string*]?
}

// Structured name components
type StructuredName = {
    family: [Text*]?,        // Family names (surnames)
    given: [Text*]?,         // Given names (first names)  
    additional: [Text*]?,    // Additional names (middle names)
    prefix: [Text*]?,        // Honorific prefixes (Mr., Dr.)
    suffix: [Text*]?         // Honorific suffixes (Jr., III)
}

// Address components
type Address = {
    pobox: Text?,           // Post office box
    ext: Text?,             // Extended address (apartment, suite)
    street: Text?,          // Street address
    locality: Text?,        // Locality (city)
    region: Text?,          // Region (state/province)
    code: Text?,            // Postal code
    country: Text?          // Country name
}

// Telephone number with parameters
type Telephone = {
    parameters: CommonParameters & {
        type: [('voice' | 'fax' | 'cell' | 'video' | 'pager' | 'textphone' | 'home' | 'work')*]?
    },
    value: Text
}

// Email with parameters
type Email = {
    parameters: CommonParameters & {
        type: [('home' | 'work' | 'internet')*]?
    },
    value: Text
}

// URL with parameters
type URL = {
    parameters: CommonParameters & {
        type: [('home' | 'work')*]?
    },
    value: URI
}

// Instant messaging addresses
type IMPP = {
    parameters: CommonParameters & {
        type: [('home' | 'work' | 'personal' | 'business')*]?
    },
    value: URI  // xmpp:alice@example.com, skype:alice.example
}

// Geographic position
type Geo = {
    parameters: CommonParameters,
    value: string  // "geo:latitude,longitude" URI or "latitude;longitude"
}

// Organizational information
type Organization = {
    parameters: CommonParameters & {
        'sort-as': [Text*]?,
        type: [('work')*]?
    },
    values: [Text*]  // Organization name, department, etc.
}

// Related person
type Related = {
    parameters: CommonParameters & {
        type: [('contact' | 'acquaintance' | 'friend' | 'met' | 'co-worker' | 'colleague' | 'co-resident' | 'neighbor' | 'child' | 'parent' | 'sibling' | 'spouse' | 'kin' | 'muse' | 'crush' | 'date' | 'sweetheart' | 'me' | 'agent' | 'emergency')*]?
    },
    value: Text | URI
}

// Photo/Logo/Sound
type MediaProperty = {
    parameters: CommonParameters & {
        mediatype: MediaType?,
        type: [('home' | 'work')*]?
    },
    value: URI | string  // URI reference or inline data
}

// Anniversary/Birthday
type DateProperty = {
    parameters: CommonParameters & {
        calscale: 'gregorian'?
    },
    value: Date | DateTime | string  // Can be partial date like "--1201" for Dec 1
}

// Gender
type Gender = {
    parameters: CommonParameters,
    sex: 'M' | 'F' | 'O' | 'N' | 'U'?,  // Male, Female, Other, None, Unknown
    identity: Text?  // Free-form gender identity
}

// Kind of entity
type Kind = 'individual' | 'group' | 'org' | 'location' | 'application' | 'device'

// Main vCard properties
type VCardProperties = {
    // Required properties
    fn: Text,               // Formatted name (required)
    version: '4.0',         // vCard version (required)
    
    // Identification properties
    n: StructuredName?,     // Structured name
    nickname: [Text*]?,     // Nicknames
    photo: [MediaProperty*]?, // Photos
    bday: DateProperty?,    // Birthday
    anniversary: DateProperty?, // Anniversary
    gender: Gender?,        // Gender
    
    // Delivery addressing properties
    adr: [Address*]?,       // Addresses
    
    // Communications properties  
    tel: [Telephone*]?,     // Telephone numbers
    email: [Email*]?,       // Email addresses
    impp: [IMPP*]?,         // Instant messaging
    lang: [LanguageTag*]?,  // Languages
    
    // Geographical properties
    tz: [Text*]?,           // Time zones
    geo: [Geo*]?,           // Geographic position
    
    // Organizational properties
    title: [Text*]?,        // Job title
    role: [Text*]?,         // Role or occupation
    logo: [MediaProperty*]?, // Logos
    org: [Organization*]?,  // Organizations
    member: [URI*]?,        // Group membership (for group vCards)
    related: [Related*]?,   // Related people
    
    // Explanatory properties
    categories: [Text*]?,   // Categories
    note: [Text*]?,         // Notes
    prodid: Text?,          // Product identifier
    rev: DateTime?,         // Revision timestamp
    sound: [MediaProperty*]?, // Sounds
    uid: URI?,              // Unique identifier
    clientpidmap: [string*]?, // Client PID map
    url: [URL*]?,           // URLs
    
    // Security properties
    key: [MediaProperty*]?, // Public keys
    
    // Calendar properties
    fburl: [URI*]?,         // Free/busy URL
    caladruri: [URI*]?,     // Calendar address URI
    caluri: [URI*]?,        // Calendar URI
    
    // Extended properties
    'x-*': Text?,           // Extension properties
    
    // vCard 4.0 specific
    kind: Kind?,            // Kind of object
    xml: [Text*]?           // XML content
}

// Complete vCard structure
type VCard = {
    version: '4.0',
    properties: VCardProperties
}

// Group of vCards (for multiple contacts in one file)
type VCardCollection = {
    vcards: [VCard+]
}

// Validation rules for vCard
type VCFValidationRules = {
    // Required properties
    required_properties: ['fn', 'version'],
    
    // Required version
    required_version: '4.0',
    
    // Valid property names
    valid_properties: [
        'fn', 'n', 'nickname', 'photo', 'bday', 'anniversary', 'gender',
        'adr', 'tel', 'email', 'impp', 'lang', 'tz', 'geo',
        'title', 'role', 'logo', 'org', 'member', 'related',
        'categories', 'note', 'prodid', 'rev', 'sound', 'uid',
        'clientpidmap', 'url', 'key', 'fburl', 'caladruri', 'caluri',
        'kind', 'xml', 'version'
    ],
    
    // Email pattern (simplified)
    email_pattern: '^[^@]+@[^@]+\\.[^@]+$',
    
    // Phone number pattern (simplified)
    phone_pattern: '^[+]?[0-9\\s\\-\\(\\)\\.\\/]+$',
    
    // URL pattern (simplified)
    url_pattern: '^https?:\\/\\/.+$',
    
    // Preference range
    preference_range: { min: 1, max: 100 },
    
    // Valid gender values
    valid_gender_sex: ['M', 'F', 'O', 'N', 'U'],
    
    // Valid kind values
    valid_kinds: ['individual', 'group', 'org', 'location', 'application', 'device']
}

// Root document type for VCF validation
type VCFDocument = VCard | VCardCollection
