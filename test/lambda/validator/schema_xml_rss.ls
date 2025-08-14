// RSS/Feed XML schema
// Tests XML for RSS feeds with specific structure

type RssItem = <item
    title: string,                    // item title
    link: string,                     // item URL
    description: string,              // item description
    pubDate: datetime?,               // publication date
    author: string?,                  // optional author
    category: string*,                // zero or more categories
    guid: string?                     // optional unique identifier
>

type RssChannel = <channel
    title: string,                    // channel title
    link: string,                     // channel URL
    description: string,              // channel description
    language: string?,                // optional language
    copyright: string?,               // optional copyright
    managingEditor: string?,          // optional editor
    webMaster: string?,               // optional webmaster
    pubDate: datetime?,               // optional publication date
    lastBuildDate: datetime?,         // optional last build date
    ttl: int?,                        // optional time to live
    RssItem*                          // zero or more items
>

type Document = <rss
    version: string,                  // RSS version (required attribute)
    RssChannel                        // single channel (required child element)
>
