#!/usr/bin/env python3
"""
Script to update the HTML5 tokenizer with the complete WHATWG entity table.
Replaces the old entity table and linear search with binary search.
"""

import re

TOKENIZER_PATH = "lambda/input/html5/html5_tokenizer.cpp"
ENTITIES_PATH = "temp/html5_entities.inc"

def read_file(path):
    with open(path, 'r') as f:
        return f.read()

def write_file(path, content):
    with open(path, 'w') as f:
        f.write(content)

def main():
    # Read the tokenizer file
    tokenizer = read_file(TOKENIZER_PATH)

    # Read the generated entities
    entities = read_file(ENTITIES_PATH)

    # Find and replace the entity table section
    # Pattern: from "// Named character entity table" to the closing brace before lookup function

    # Find the start marker
    start_marker = "// Named character entity table"
    start_idx = tokenizer.find(start_marker)
    if start_idx == -1:
        print("ERROR: Could not find start marker")
        return 1

    # Find the end of the table (the line with {nullptr, nullptr})
    end_marker = "{nullptr, nullptr}"
    end_idx = tokenizer.find(end_marker, start_idx)
    if end_idx == -1:
        print("ERROR: Could not find end marker")
        return 1

    # Find the closing brace and semicolon after nullptr
    end_idx = tokenizer.find("};", end_idx) + 2

    # Build new entity section
    new_entity_section = f"""{entities}
"""

    # Replace the entity table
    new_tokenizer = tokenizer[:start_idx] + new_entity_section + tokenizer[end_idx:]

    # Now replace the lookup function with binary search
    old_lookup = """// Look up named entity (case-sensitive)
static const char* html5_lookup_named_entity(const char* name, size_t len) {
    for (const NamedEntity* e = named_entities; e->name != nullptr; e++) {
        if (strlen(e->name) == len && memcmp(e->name, name, len) == 0) {
            return e->replacement;
        }
    }
    return nullptr;
}"""

    new_lookup = """// Entity count for binary search
static const size_t NAMED_ENTITY_COUNT = 2125;

// Look up named entity using binary search (case-sensitive)
// Table is sorted alphabetically by name
static const char* html5_lookup_named_entity(const char* name, size_t len) {
    size_t low = 0;
    size_t high = NAMED_ENTITY_COUNT;

    while (low < high) {
        size_t mid = low + (high - low) / 2;
        const char* mid_name = named_entities[mid].name;
        size_t mid_len = strlen(mid_name);

        // Compare by length first, then by content
        int cmp;
        size_t min_len = len < mid_len ? len : mid_len;
        cmp = memcmp(name, mid_name, min_len);
        if (cmp == 0) {
            // If equal up to min_len, shorter string comes first
            if (len < mid_len) cmp = -1;
            else if (len > mid_len) cmp = 1;
        }

        if (cmp < 0) {
            high = mid;
        } else if (cmp > 0) {
            low = mid + 1;
        } else {
            return named_entities[mid].replacement;
        }
    }
    return nullptr;
}"""

    if old_lookup in new_tokenizer:
        new_tokenizer = new_tokenizer.replace(old_lookup, new_lookup)
        print("Replaced lookup function with binary search")
    else:
        print("WARNING: Could not find old lookup function to replace")
        # Try to find it with different whitespace
        lookup_pattern = r"// Look up named entity.*?return nullptr;\s*\}"
        match = re.search(lookup_pattern, new_tokenizer, re.DOTALL)
        if match:
            new_tokenizer = new_tokenizer[:match.start()] + new_lookup + new_tokenizer[match.end():]
            print("Replaced lookup function (regex match)")
        else:
            print("ERROR: Could not replace lookup function")

    # Write the updated tokenizer
    write_file(TOKENIZER_PATH, new_tokenizer)
    print(f"Updated {TOKENIZER_PATH}")
    print(f"Entity table now has 2125 entries (was ~247)")

    return 0

if __name__ == "__main__":
    exit(main())
