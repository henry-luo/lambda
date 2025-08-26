// RSS/Feed XML schema - Fixed
// Tests XML for RSS feeds with specific structure

// Root RSS element - defined first to be recognized as root
type Document = <rss
    version: string,                  // RSS version (required attribute)
    RssChannel                        // single channel (required child element)
>

// Channel contains child elements, not attributes
type RssChannel = <channel
    RssTitle,                         // required title child element
    RssLink,                          // required link child element
    RssDescription,                   // required description child element
    RssLanguage?,                     // optional language child element
    RssCopyright?,                    // optional copyright child element
    RssManagingEditor?,               // optional managingEditor child element
    RssWebMaster?,                    // optional webMaster child element
    RssPubDate?,                      // optional pubDate child element
    RssLastBuildDate?,                // optional lastBuildDate child element
    RssTtl?,                          // optional ttl child element
    RssItem*                          // zero or more item child elements
>

// Define child element types with text content
type RssTitle = <title string>
type RssLink = <link string>
type RssDescription = <description string>
type RssLanguage = <language string>
type RssCopyright = <copyright string>
type RssManagingEditor = <managingEditor string>
type RssWebMaster = <webMaster string>
type RssPubDate = <pubDate string>
type RssLastBuildDate = <lastBuildDate string>
type RssTtl = <ttl string>

// Item contains child elements, not attributes
type RssItem = <item
    RssItemTitle,                     // required title child element
    RssItemLink,                      // required link child element
    RssItemDescription,               // required description child element
    RssItemPubDate?,                  // optional pubDate child element
    RssItemAuthor?,                   // optional author child element
    RssItemCategory*,                 // zero or more category child elements
    RssItemGuid?                      // optional guid child element
>

// Define item child element types with text content
type RssItemTitle = <title string>
type RssItemLink = <link string>
type RssItemDescription = <description string>
type RssItemPubDate = <pubDate string>
type RssItemAuthor = <author string>
type RssItemCategory = <category string>
type RssItemGuid = <guid string>
