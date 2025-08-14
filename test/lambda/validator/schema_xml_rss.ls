// RSS/Feed XML schema - Fixed
// Tests XML for RSS feeds with specific structure

// Root RSS element - defined first to be recognized as root
type Document = <rss
    version: string,                  // RSS version (required attribute)
    RssChannel                        // single channel (required child element)
>

type RssChannel = <channel
    title: string,                    // channel title (text content)
    link: string,                     // channel URL (text content)
    description: string,              // channel description (text content)
    language: string?,                // optional language (text content)
    copyright: string?,               // optional copyright (text content)
    managingEditor: string?,          // optional editor (text content)
    webMaster: string?,               // optional webmaster (text content)
    pubDate: string?,                 // optional publication date (as string)
    lastBuildDate: string?,           // optional last build date (as string)
    ttl: string?,                     // optional time to live (as string)
    RssItem*                          // zero or more items
>

type RssItem = <item
    title: string,                    // item title (text content)
    link: string,                     // item URL (text content)
    description: string,              // item description (text content)
    pubDate: string?,                 // publication date (as string)
    author: string?,                  // optional author (text content)
    category: string?,                // single category (text content) - simplified
    guid: string?                     // optional unique identifier (text content)
>
