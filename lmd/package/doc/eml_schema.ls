// EML Schema Definition in Lambda Script
// Comprehensive schema for Email Message Format (RFC 5322)
// Defines structure for email messages with headers and body content

// Basic data types for email
type EmailAddress = string
type MimeType = string
type Encoding = string
type Charset = string
type DateTime = string
type MessageId = string

// Email header fields
type EmailHeaders = {
    // Essential headers
    from: EmailAddress?,
    to: [EmailAddress*]?,
    cc: [EmailAddress*]?,
    bcc: [EmailAddress*]?,
    subject: string?,
    date: DateTime?,
    'message-id': MessageId?,
    
    // Reply and reference headers
    'reply-to': EmailAddress?,
    'in-reply-to': MessageId?,
    references: [MessageId*]?,
    
    // Content headers
    'content-type': MimeType?,
    'content-encoding': Encoding?,
    'content-disposition': string?,
    'content-transfer-encoding': Encoding?,
    
    // Routing headers
    received: [string*]?,
    'return-path': EmailAddress?,
    'delivered-to': EmailAddress?,
    
    // Optional headers
    sender: EmailAddress?,
    'reply-to': EmailAddress?,
    comments: string?,
    keywords: [string*]?,
    
    // Priority and importance
    priority: 'high' | 'normal' | 'low'?,
    importance: 'high' | 'normal' | 'low'?,
    'x-priority': '1' | '2' | '3' | '4' | '5'?,
    
    // MIME version
    'mime-version': string?,
    
    // Custom headers (X-*)
    'x-*': string?
}

// Email body content types
type TextContent = {
    type: 'text/plain' | 'text/html',
    charset: Charset?,
    encoding: Encoding?,
    string
}

type Attachment = {
    filename: string?,
    'content-type': MimeType,
    'content-disposition': 'attachment' | 'inline'?,
    'content-transfer-encoding': Encoding?,
    'content-id': string?,
    size: int?,
    string  // Base64 encoded or raw content
}

type MultipartContent = {
    type: 'multipart/mixed' | 'multipart/alternative' | 'multipart/related',
    boundary: string,
    parts: [EmailPart*]
}

type EmailPart = TextContent | Attachment | MultipartContent

type EmailBody = {
    EmailPart
}

// Complete email message structure
type EmailMessage = {
    headers: EmailHeaders,
    body: EmailBody
}

// Validation rules for email messages
type EmailValidationRules = {
    // Required headers for valid email
    required_headers: ['from'],
    
    // Valid email address pattern (simplified)
    email_pattern: '^[^@]+@[^@]+\\.[^@]+$',
    
    // Maximum header line length (RFC 5322)
    max_header_line_length: 998,
    
    // Maximum body line length
    max_body_line_length: 1000,
    
    // Allowed content transfer encodings
    allowed_encodings: ['7bit', '8bit', 'binary', 'quoted-printable', 'base64'],
    
    // Common MIME types
    common_mime_types: [
        'text/plain', 'text/html', 'text/css', 'text/javascript',
        'application/pdf', 'application/msword', 'application/zip',
        'image/jpeg', 'image/png', 'image/gif', 'image/svg+xml',
        'audio/mpeg', 'audio/wav', 'video/mp4', 'video/mpeg'
    ]
}

// Root document type for EML validation
type EMLDocument = EmailMessage
