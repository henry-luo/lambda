// MIME type database extracted and simplified from Apache Tika's tika-mimetypes.xml
// This file contains the most common MIME type patterns for detection

#include "mime-detect.h"
#include <stddef.h>

// Magic patterns sorted by priority (higher priority = more specific)
MimePattern magic_patterns[] = {
    // PDF
    {"%PDF-", 5, 0, 50, "application/pdf"},
    {"\xef\xbb\xbf%PDF-", 8, 0, 50, "application/pdf"},

    // Office documents (OLE2)
    {"\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1", 8, 0, 50, "application/x-tika-msoffice"},

    // ZIP (and Office Open XML)
    {"PK\x03\x04", 4, 0, 50, "application/zip", "application/zip", 70},
    {"PK\x05\x06", 4, 0, 50, "application/zip"},

    // JPEG
    {"\xff\xd8\xff", 3, 0, 50, "image/jpeg", "image/jpeg", 80},

    // PNG
    {"\x89PNG\r\n\x1a\n", 8, 0, 50, "image/png", "image/png", 80},

    // GIF
    {"GIF87a", 6, 0, 50, "image/gif", "image/gif", 80},
    {"GIF89a", 6, 0, 50, "image/gif", "image/gif", 80},

    // TIFF
    {"MM\x00\x2a", 4, 0, 50, "image/tiff"},
    {"II\x2a\x00", 4, 0, 50, "image/tiff"},

    // BMP
    {"BM", 2, 0, 50, "image/bmp", "image/bmp", 40},

    // WebP
    {"RIFF", 4, 0, 40, "image/webp", "image/webp", 30}, // needs further validation

    // HTML
    {"<!DOCTYPE html>", 15, 0, 60, "text/html"},
    {"<!doctype html>", 15, 0, 60, "text/html"},
    {"<!DOCTYPE html", 14, 0, 60, "text/html"},  // Without closing >
    {"<!doctype html", 14, 0, 60, "text/html"},  // Without closing >
    {"<html", 5, 0, 50, "text/html"},
    {"<HTML", 5, 0, 50, "text/html"},
    {"<head", 5, 0, 50, "text/html"},
    {"<HEAD", 5, 0, 50, "text/html"},
    {"<body", 5, 0, 50, "text/html"},
    {"<BODY", 5, 0, 50, "text/html"},

    // XML
    {"<?xml", 5, 0, 50, "application/xml"},
    {"<?XML", 5, 0, 50, "application/xml"},
    {"\xef\xbb\xbf<?xml", 8, 0, 50, "application/xml"}, // UTF-8 BOM

    // JSON
    {"{", 1, 0, 30, "application/json"},
    {"[", 1, 0, 30, "application/json"},

    // Plain text (fallback)
        // XML
    {"<?xml", 5, 0, 50, "application/xml"},

    // Shell scripts and Python
    {"#!/bin/bash", 11, 0, 55, "application/x-shellscript"},
    {"#!/bin/sh", 9, 0, 55, "application/x-shellscript"},
    {"#!/usr/bin/env python", 20, 0, 55, "text/x-python"},
    {"#!/usr/bin/python", 16, 0, 55, "text/x-python"},

    // Plain text (fallback)
    {"\xef\xbb\xbf", 3, 0, 10, "text/plain"}, // UTF-8 BOM

    // RTF
    {"{\\rtf", 5, 0, 50, "application/rtf"},

    // PostScript
    {"%!", 2, 0, 50, "application/postscript"},
    {"\x04%!", 3, 0, 50, "application/postscript"},

    // Gzip
    {"\x1f\x8b", 2, 0, 50, "application/gzip", "application/gzip", 70},

    // Bzip2
    {"BZh", 3, 0, 40, "application/x-bzip2"},

    // 7zip
    {"7z\xbc\xaf\x27\x1c", 6, 0, 50, "application/x-7z-compressed", "application/x-7z-compressed", 70},

    // RAR
    {"Rar!\x1a\x07\x00", 7, 0, 50, "application/x-rar-compressed"},
    {"Rar!\x1a\x07\x01\x00", 8, 0, 50, "application/x-rar-compressed"},

    // TAR
    {"ustar\x00", 6, 257, 40, "application/x-tar"},

    // EPUB
    {"PK\x03\x04", 4, 0, 30, "application/epub+zip"}, // needs mimetype validation

    // MP3
    {"ID3", 3, 0, 50, "audio/mpeg", "audio/mpeg", 70},
    {"\xff\xfb", 2, 0, 40, "audio/mpeg", "audio/mpeg", 50},
    {"\xff\xfa", 2, 0, 40, "audio/mpeg"},

    // MP4/MOV
    {"ftyp", 4, 4, 60, "video/mp4"},

    // WAV
    {"RIFF", 4, 0, 40, "audio/wav"}, // needs WAVE validation

    // SVG
    {"<svg", 4, 0, 50, "image/svg+xml"},

    // CSV (basic detection)
    {",", 1, 0, 20, "text/csv"}, // very weak signal

    // Markdown (basic detection)
    {"# ", 2, 0, 30, "text/markdown"},
    {"## ", 3, 0, 30, "text/markdown"},
    {"### ", 4, 0, 30, "text/markdown"},

    // vCard
    {"BEGIN:VCARD", 11, 0, 60, "text/vcard"},

    // Serve-only signatures; negative input priority excludes them from input().
    {"%PDF", 4, 0, -1, NULL, "application/pdf", 90},
    {"OggS", 4, 0, -1, NULL, "audio/ogg", 70},
    {"fLaC", 4, 0, -1, NULL, "audio/flac", 70},
    {"Rar!\x1a\x07", 6, 0, -1, NULL, "application/vnd.rar", 70},
    {"\xfd""7zXZ\0", 6, 0, -1, NULL, "application/x-xz", 70},
    {"\x7f""ELF", 4, 0, -1, NULL, "application/x-elf", 90},
    {"\xfe\xed\xfa", 3, 0, -1, NULL, "application/x-mach-binary", 90},
    {"MZ", 2, 0, -1, NULL, "application/x-dosexec", 60},
    {"\0asm", 4, 0, -1, NULL, "application/wasm", 90},
};

// File extension patterns
MimeGlob glob_patterns[] = {
    // Documents
    {"*.pdf", "application/pdf", "application/pdf"},
    {"*.doc", "application/msword", "application/msword"},
    {"*.docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {"*.xls", "application/vnd.ms-excel", "application/vnd.ms-excel"},
    {"*.xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
    {"*.ppt", "application/vnd.ms-powerpoint", "application/vnd.ms-powerpoint"},
    {"*.pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
    {"*.odt", "application/vnd.oasis.opendocument.text"},
    {"*.ods", "application/vnd.oasis.opendocument.spreadsheet"},
    {"*.odp", "application/vnd.oasis.opendocument.presentation"},
    {"*.rtf", "application/rtf", "application/rtf"},

    // Text
    {"*.txt", "text/plain", "text/plain"},
    {"*.csv", "text/csv", "text/csv"},
    {"*.tsv", "text/tab-separated-values"},
    {"*.html", "text/html", "text/html"},
    {"*.htm", "text/html", "text/html"},
    {"*.xml", "application/xml", "application/xml"},
    {"*.json", "application/json", "application/json"},
    {"*.yaml", "application/x-yaml", "text/yaml"},
    {"*.yml", "application/x-yaml", "text/yaml"},
    {"*.toml", "application/toml", "application/toml"},
    {"*.ini", "text/plain", "text/plain"},
    {"*.properties", "text/x-java-properties"},
    {"*.props", "text/x-java-properties"},
    {"*.md", "text/markdown", "text/markdown"},
    {"*.markdown", "text/markdown"},
    {"*.mdx", "text/mdx"},
    {"*.rst", "text/x-rst"},
    {"*.org", "text/x-org"},
    {"*.asciidoc", "text/x-asciidoc"},
    {"*.adoc", "text/x-asciidoc"},
    {"*.asc", "text/x-asciidoc"},
    {"*.wiki", "text/x-wiki"},
    {"*.1", "text/troff"},
    {"*.2", "text/troff"},
    {"*.3", "text/troff"},
    {"*.4", "text/troff"},
    {"*.5", "text/troff"},
    {"*.6", "text/troff"},
    {"*.7", "text/troff"},
    {"*.8", "text/troff"},
    {"*.9", "text/troff"},
    {"*.man", "text/troff"},
    {"*.tex", "application/x-tex", "application/x-latex"},
    {"*.latex", "application/x-latex"},
    {"*.typ", "text/typst"},
    {"*.typst", "text/typst"},
    {"*.vcf", "text/vcard"},
    {"*.vcard", "text/vcard"},
    {"*.ics", "text/calendar"},
    {"*.ical", "text/calendar"},
    {"*.textile", "text/textile"},
    {"*.txtl", "text/textile"},
    {"*.m", "text/x-mark"},
    {"*.mk", "text/x-mark"},
    {"*.mark", "text/x-mark"},

    // Programming languages
    {"*.c", "text/x-c", "text/x-c"},
    {"*.h", "text/x-c", "text/x-c"},
    {"*.cpp", "text/x-c++src", "text/x-c++"},
    {"*.cxx", "text/x-c++src"},
    {"*.cc", "text/x-c++src"},
    {"*.hpp", "text/x-c++hdr", "text/x-c++"},
    {"*.hxx", "text/x-c++hdr"},
    {"*.java", "text/x-java-source", "text/x-java"},
    {"*.py", "text/x-python", "text/x-python"},
    {"*.js", "application/javascript", "application/javascript"},
    {"*.ts", "application/typescript"},
    {"*.php", "application/x-httpd-php"},
    {"*.rb", "application/x-ruby", "text/x-ruby"},
    {"*.pl", "application/x-perl"},
    {"*.sh", "application/x-sh", "application/x-sh"},
    {"*.bash", "application/x-bash"},
    {"*.css", "text/css", "text/css"},
    {"*.scss", "text/x-scss"},
    {"*.less", "text/x-less"},
    {"*.sql", "text/x-sql"},

    // Images
    {"*.jpg", "image/jpeg", "image/jpeg"},
    {"*.jpeg", "image/jpeg", "image/jpeg"},
    {"*.png", "image/png", "image/png"},
    {"*.gif", "image/gif", "image/gif"},
    {"*.bmp", "image/bmp", "image/bmp"},
    {"*.tiff", "image/tiff", "image/tiff"},
    {"*.tif", "image/tiff", "image/tiff"},
    {"*.webp", "image/webp", "image/webp"},
    {"*.svg", "image/svg+xml", "image/svg+xml"},
    {"*.ico", "image/vnd.microsoft.icon", "image/x-icon"},
    {"*.psd", "image/vnd.adobe.photoshop"},

    // Audio
    {"*.mp3", "audio/mpeg", "audio/mpeg"},
    {"*.wav", "audio/wav", "audio/wav"},
    {"*.ogg", "audio/ogg", "audio/ogg"},
    {"*.flac", "audio/flac", "audio/flac"},
    {"*.aac", "audio/aac", "audio/aac"},
    {"*.m4a", "audio/mp4", "audio/mp4"},
    {"*.wma", "audio/x-ms-wma"},

    // Video
    {"*.mp4", "video/mp4", "video/mp4"},
    {"*.avi", "video/x-msvideo", "video/x-msvideo"},
    {"*.mov", "video/quicktime", "video/quicktime"},
    {"*.wmv", "video/x-ms-wmv"},
    {"*.flv", "video/x-flv"},
    {"*.webm", "video/webm", "video/webm"},
    {"*.mkv", "video/x-matroska", "video/x-matroska"},
    {"*.3gp", "video/3gpp"},

    // Archives
    {"*.zip", "application/zip", "application/zip"},
    {"*.rar", "application/x-rar-compressed", "application/vnd.rar"},
    {"*.7z", "application/x-7z-compressed", "application/x-7z-compressed"},
    {"*.tar", "application/x-tar", "application/x-tar"},
    {"*.gz", "application/gzip", "application/gzip"},
    {"*.bz2", "application/x-bzip2", "application/x-bzip2"},
    {"*.xz", "application/x-xz", "application/x-xz"},

    // Ebooks
    {"*.epub", "application/epub+zip"},
    {"*.mobi", "application/x-mobipocket-ebook"},
    {"*.azw", "application/vnd.amazon.ebook"},

    // Fonts
    {"*.ttf", "font/ttf", "font/ttf"},
    {"*.otf", "font/otf", "font/otf"},
    {"*.woff", "font/woff", "font/woff"},
    {"*.woff2", "font/woff2", "font/woff2"},
    {"*.eot", "application/vnd.ms-fontobject", "application/vnd.ms-fontobject"},

    // CAD
    {"*.dwg", "image/vnd.dwg"},
    {"*.dxf", "image/vnd.dxf"},

    // 3D
    {"*.stl", "model/stl"},
    {"*.obj", "model/obj"},
    {"*.3mf", "model/3mf"},

    // Executables
    {"*.exe", "application/x-msdownload"},
    {"*.msi", "application/x-ms-installer"},
    {"*.deb", "application/vnd.debian.binary-package"},
    {"*.rpm", "application/x-rpm"},
    {"*.dmg", "application/x-apple-diskimage"},

    // Data
    {"*.sqlite", "application/x-sqlite3"},
    {"*.db", "application/x-sqlite3"},
    {"*.mdb", "application/x-msaccess"},
    // Serve-only filename types retain their existing MIME spellings.
    {"*.mjs", NULL, "application/javascript"},
    {"*.wasm", NULL, "application/wasm"},
    {"*.log", NULL, "text/plain"},
    {"*.ls", NULL, "text/x-lambda"},
    {"*.avif", NULL, "image/avif"},
    {"*.go", NULL, "text/x-go"},
    {"*.rs", NULL, "text/x-rust"},
    {"*.bat", NULL, "application/x-msdos-program"},

};

const size_t MAGIC_PATTERNS_COUNT = sizeof(magic_patterns) / sizeof(magic_patterns[0]);
const size_t GLOB_PATTERNS_COUNT = sizeof(glob_patterns) / sizeof(glob_patterns[0]);
