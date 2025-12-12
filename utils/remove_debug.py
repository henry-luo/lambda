#!/usr/bin/env python3
"""Remove debug fprintf statements from format_latex_html_v2.cpp"""

import re

# Read the file
with open('lambda/format/format_latex_html_v2.cpp', 'r') as f:
    content = f.read()

# Pattern to match fprintf statements (including multi-line)
# Match fprintf(stderr, ... ); including everything up to the semicolon
pattern = r'fprintf\(stderr,[^;]*\);'

# Remove all matches
content = re.sub(pattern, '', content, flags=re.MULTILINE | re.DOTALL)

# Write back
with open('lambda/format/format_latex_html_v2.cpp', 'w') as f:
    f.write(content)

print("Debug fprintf statements removed successfully")
