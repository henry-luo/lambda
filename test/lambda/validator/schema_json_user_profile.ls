// JSON Schema converted to Lambda schema
// Based on user-profile.schema.json - comprehensive user profile management

// Address with required and optional fields
type AddressType = {
    street: string,                   // required street address
    city: string,                     // required city name
    state: string?,                   // optional state/province
    zipCode: string?,                 // optional ZIP code with pattern validation
    country: string                   // required 2-letter country code
}

// Notification preferences
type NotificationPreferences = {
    email: bool?,                     // optional email notifications
    sms: bool?,                       // optional SMS notifications
    push: bool?                       // optional push notifications
}

// User preferences and settings
type UserPreferences = {
    theme: string?,                   // optional theme: "light", "dark", "auto"
    language: string?,                // optional language: "en", "es", "fr", "de", "ja", "zh"
    notifications: NotificationPreferences?, // optional notification settings
    newsletter: bool?                 // optional newsletter subscription
}

// Status can be string or int (union type for testing)
type UserStatus = string | int

// User profile information
type UserProfile = {
    firstName: string,                // required first name (1-50 chars)  
    lastName: string,                 // required last name (1-50 chars)
    dateOfBirth: string?,            // optional date in YYYY-MM-DD format
    phoneNumber: string?,            // optional phone with international format
    address: AddressType?,           // optional address information
    status: UserStatus               // union type: string status or int code
}

// User metadata and system information
type UserMetadata = {
    createdAt: string,               // required creation timestamp (ISO date-time)
    updatedAt: string?,              // optional last update timestamp
    lastLogin: string?,              // optional last login timestamp
    isActive: bool,                  // required active status flag
    loginCount: int?                 // optional login count (minimum 0)
}

// Main user profile document
type UserProfileDocument = {
    id: int,                         // required unique user ID (minimum 1)
    username: string,                // required username (3-20 alphanumeric + underscore)
    email: string,                   // required email address
    profile: UserProfile,            // required profile information
    preferences: UserPreferences?,   // optional user preferences
    roles: [string]+,                // one or more roles: "user", "admin", "moderator", "premium" (array)
    tags: [string]*,                 // zero or more tags (max 10) (array)
    metadata: UserMetadata           // required metadata
}
