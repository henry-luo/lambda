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
    {"PK\x03\x04", 4, 0, 50, "application/zip"},
    {"PK\x05\x06", 4, 0, 50, "application/zip"},

    // JPEG
    {"\xff\xd8\xff", 3, 0, 50, "image/jpeg"},

    // PNG
    {"\x89PNG\r\n\x1a\n", 8, 0, 50, "image/png"},

    // GIF
    {"GIF87a", 6, 0, 50, "image/gif"},
    {"GIF89a", 6, 0, 50, "image/gif"},

    // TIFF
    {"MM\x00\x2a", 4, 0, 50, "image/tiff"},
    {"II\x2a\x00", 4, 0, 50, "image/tiff"},

    // BMP
    {"BM", 2, 0, 50, "image/bmp"},

    // WebP
    {"RIFF", 4, 0, 40, "image/webp"}, // needs further validation

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
    {"\x1f\x8b", 2, 0, 50, "application/gzip"},

    // Bzip2
    {"BZh", 3, 0, 40, "application/x-bzip2"},

    // 7zip
    {"7z\xbc\xaf\x27\x1c", 6, 0, 50, "application/x-7z-compressed"},

    // RAR
    {"Rar!\x1a\x07\x00", 7, 0, 50, "application/x-rar-compressed"},
    {"Rar!\x1a\x07\x01\x00", 8, 0, 50, "application/x-rar-compressed"},

    // TAR
    {"ustar\x00", 6, 257, 40, "application/x-tar"},

    // EPUB
    {"PK\x03\x04", 4, 0, 30, "application/epub+zip"}, // needs mimetype validation

    // MP3
    {"ID3", 3, 0, 50, "audio/mpeg"},
    {"\xff\xfb", 2, 0, 40, "audio/mpeg"},
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
};

// File extension patterns
MimeGlob glob_patterns[] = {
    // Documents
    {"*.pdf", "application/pdf"},
    {"*.doc", "application/msword"},
    {"*.docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {"*.xls", "application/vnd.ms-excel"},
    {"*.xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
    {"*.ppt", "application/vnd.ms-powerpoint"},
    {"*.pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
    {"*.odt", "application/vnd.oasis.opendocument.text"},
    {"*.ods", "application/vnd.oasis.opendocument.spreadsheet"},
    {"*.odp", "application/vnd.oasis.opendocument.presentation"},
    {"*.rtf", "application/rtf"},

    // Text
    {"*.txt", "text/plain"},
    {"*.csv", "text/csv"},
    {"*.tsv", "text/tab-separated-values"},
    {"*.html", "text/html"},
    {"*.htm", "text/html"},
    {"*.xml", "application/xml"},
    {"*.json", "application/json"},
    {"*.yaml", "application/x-yaml"},
    {"*.yml", "application/x-yaml"},
    {"*.toml", "application/toml"},
    {"*.ini", "text/plain"},
    {"*.properties", "text/x-java-properties"},
    {"*.props", "text/x-java-properties"},
    {"*.md", "text/markdown"},
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
    {"*.tex", "application/x-tex"},
    {"*.latex", "application/x-latex"},
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
    {"*.c", "text/x-c"},
    {"*.h", "text/x-c"},
    {"*.cpp", "text/x-c++src"},
    {"*.cxx", "text/x-c++src"},
    {"*.cc", "text/x-c++src"},
    {"*.hpp", "text/x-c++hdr"},
    {"*.hxx", "text/x-c++hdr"},
    {"*.java", "text/x-java-source"},
    {"*.py", "text/x-python"},
    {"*.js", "application/javascript"},
    {"*.ts", "application/typescript"},
    {"*.php", "application/x-httpd-php"},
    {"*.rb", "application/x-ruby"},
    {"*.pl", "application/x-perl"},
    {"*.sh", "application/x-sh"},
    {"*.bash", "application/x-bash"},
    {"*.css", "text/css"},
    {"*.scss", "text/x-scss"},
    {"*.less", "text/x-less"},
    {"*.sql", "text/x-sql"},

    // Images
    {"*.jpg", "image/jpeg"},
    {"*.jpeg", "image/jpeg"},
    {"*.png", "image/png"},
    {"*.gif", "image/gif"},
    {"*.bmp", "image/bmp"},
    {"*.tiff", "image/tiff"},
    {"*.tif", "image/tiff"},
    {"*.webp", "image/webp"},
    {"*.svg", "image/svg+xml"},
    {"*.ico", "image/vnd.microsoft.icon"},
    {"*.psd", "image/vnd.adobe.photoshop"},

    // Audio
    {"*.mp3", "audio/mpeg"},
    {"*.wav", "audio/wav"},
    {"*.ogg", "audio/ogg"},
    {"*.flac", "audio/flac"},
    {"*.aac", "audio/aac"},
    {"*.m4a", "audio/mp4"},
    {"*.wma", "audio/x-ms-wma"},

    // Video
    {"*.mp4", "video/mp4"},
    {"*.avi", "video/x-msvideo"},
    {"*.mov", "video/quicktime"},
    {"*.wmv", "video/x-ms-wmv"},
    {"*.flv", "video/x-flv"},
    {"*.webm", "video/webm"},
    {"*.mkv", "video/x-matroska"},
    {"*.3gp", "video/3gpp"},

    // Archives
    {"*.zip", "application/zip"},
    {"*.rar", "application/x-rar-compressed"},
    {"*.7z", "application/x-7z-compressed"},
    {"*.tar", "application/x-tar"},
    {"*.gz", "application/gzip"},
    {"*.bz2", "application/x-bzip2"},
    {"*.xz", "application/x-xz"},

    // Ebooks
    {"*.epub", "application/epub+zip"},
    {"*.mobi", "application/x-mobipocket-ebook"},
    {"*.azw", "application/vnd.amazon.ebook"},

    // Fonts
    {"*.ttf", "font/ttf"},
    {"*.otf", "font/otf"},
    {"*.woff", "font/woff"},
    {"*.woff2", "font/woff2"},
    {"*.eot", "application/vnd.ms-fontobject"},

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
};

const size_t MAGIC_PATTERNS_COUNT = sizeof(magic_patterns) / sizeof(magic_patterns[0]);
const size_t GLOB_PATTERNS_COUNT = sizeof(glob_patterns) / sizeof(glob_patterns[0]);
