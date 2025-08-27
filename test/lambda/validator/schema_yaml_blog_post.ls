// Yamale schema converted to Lambda schema
// Based on blog-post.yamale - blog post management system

// Author information with validation constraints  
type AuthorType = {
    id: int,                          // required author ID (minimum 1)
    username: string,                 // required username (3-20 alphanumeric + underscore)
    displayName: string,              // required display name (1-50 chars)
    email: string,                    // required email with pattern validation
    bio: string?,                     // optional bio (max 500 chars)
    avatar: string?,                  // optional avatar URL
    socialLinks: {                    // optional social media links
        twitter: string?,             // optional Twitter handle
        linkedin: string?,            // optional LinkedIn profile
        github: string?,              // optional GitHub profile
        website: string?              // optional personal website
    }?,
    verified: bool,                   // required verification status
    joinDate: string                  // required join date (YYYY-MM-DD format)
}

// Post metadata and statistics
type PostMetadataType = {
    wordCount: int,                   // required word count (minimum 0)
    readingTime: int,                 // required reading time in minutes (minimum 1)
    language: string,                 // required language: "en", "es", "fr", "de", "ja", "zh", "pt", "ru"
    difficulty: string,               // required difficulty: "beginner", "intermediate", "advanced"
    featured: bool,                   // required featured status
    sticky: bool,                     // required sticky status  
    allowComments: bool,              // required comment permission
    viewCount: int,                   // required view count (minimum 0)
    likeCount: int,                   // required like count (minimum 0)
    shareCount: int                   // required share count (minimum 0)
}

// Comment with recursive replies support
type CommentType = {
    id: int,                          // required comment ID (minimum 1)
    authorName: string,               // required author name (1-50 chars)
    authorEmail: string,              // required email with pattern validation
    content: string,                  // required comment content (1-2000 chars)
    timestamp: string,                // required timestamp (ISO date-time)
    approved: bool,                   // required approval status
    replies: [CommentType]*,          // zero or more nested replies (array with recursion)
    parentId: int?                    // optional parent comment ID
}

// Open Graph metadata for social media
type OpenGraphType = {
    title: string,                    // required OG title (max 60 chars)
    description: string,              // required OG description (max 160 chars)
    image: string,                    // required OG image URL
    imageAlt: string?,                // optional image alt text (max 100 chars)
    type: string,                     // required OG type: "article", "website", "blog"
    locale: string?                   // optional locale (pattern: en_US format)
}

// SEO and metadata information
type SeoDataType = {
    metaTitle: string?,               // optional meta title (max 60 chars)
    metaDescription: string?,         // optional meta description (max 160 chars)
    canonicalUrl: string?,            // optional canonical URL
    openGraph: OpenGraphType?,        // optional Open Graph data
    schema: {string: string}?         // optional structured data (map of any values)
}

// Status can be string or int (union type for testing)
type PostStatus = string

// Main blog post document
type BlogPostDocument = {
    title: string,                    // required title (5-100 chars)
    slug: string,                     // required URL slug (alphanumeric + hyphens)
    author: AuthorType,               // required author information
    content: string,                  // required post content (minimum 50 chars)
    excerpt: string?,                 // optional excerpt (max 500 chars)
    published: bool,                  // required publication status
    publishDate: string?,             // optional publish date (YYYY-MM-DD)
    lastModified: string?,            // optional last modified timestamp
    tags: [string]+,                  // one or more tags (1-30 chars each, max 10 tags)
    categories: [string]+,            // one or more categories (max 3)
    metadata: PostMetadataType,       // required post metadata
    comments: [CommentType]*,         // zero or more comments (array)
    relatedPosts: [int]*,             // zero or more related post IDs (max 5)
    seo: SeoDataType?,                // optional SEO data
    status: PostStatus                // required status: draft, published, archived, scheduled
}

// Root document type for validation
type Document = BlogPostDocument
