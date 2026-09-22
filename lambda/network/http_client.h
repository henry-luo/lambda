#pragma once
#ifndef LAMBDA_NETWORK_HTTP_CLIENT_H
#define LAMBDA_NETWORK_HTTP_CLIENT_H

// primary documents and dependent resources use a browser-compatible profile;
// bot-only product tokens are rejected by otherwise public documentation sites.
#define RADIANT_HTTP_CLIENT_USER_AGENT \
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) " \
    "AppleWebKit/537.36 (KHTML, like Gecko) " \
    "Chrome/140.0.0.0 Safari/537.36"

// top-level document requests also need the browser navigation headers that
// public documentation CDNs use when deciding whether to serve HTML.
#define RADIANT_HTTP_DOCUMENT_ACCEPT_HEADER \
    "Accept: text/html,application/xhtml+xml,application/xml;q=0.9," \
    "image/avif,image/webp,image/apng,*/*;q=0.8"
#define RADIANT_HTTP_DOCUMENT_LANGUAGE_HEADER "Accept-Language: en-US,en;q=0.9"

#endif
