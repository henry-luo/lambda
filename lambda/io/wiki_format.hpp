#ifndef LAMBDA_WIKI_FORMAT_HPP
#define LAMBDA_WIKI_FORMAT_HPP
#include <string.h>

// Routing only: wiki serialization belongs to lambda.doc.wiki (D7.1.1).
inline const char* wiki_format_flavor(const char* type, const char* flavor = nullptr) {
    if (!type) return nullptr;
    if (!strcmp(type, "wiki")) return !flavor || !*flavor || !strcmp(flavor, "standard") || !strcmp(flavor, "wiki")
        ? "mediawiki" : flavor;
    if (!strncmp(type, "wiki:", 5)) return type + 5;
    if (!strncmp(type, "markup:", 7)) return wiki_format_flavor(type + 7);
    if (!strcmp(type, "markup")) return wiki_format_flavor(flavor);
    static const char* names[] = {"mediawiki", "dokuwiki", "tikiwiki", "twiki", "vimwiki",
        "xwiki", "zimwiki", "creole", "jira", "confluence"};
    for (const char* name : names) if (!strcmp(type, name)) return name;
    return nullptr;
}
#endif
