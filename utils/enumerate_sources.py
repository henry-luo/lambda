#!/usr/bin/env python3
"""Generate source file cache for Lambda premake generator.
Run: python3 utils/enumerate_sources.py
Creates: temp/source_files.json
"""
import json, glob, os

with open('build_lambda_config.json') as f:
    config = json.load(f)

source_dirs = config.get('source_dirs', [])
excludes = config.get('exclude_source_files', [])

# Add platform-specific exclusions (macOS)
macos = config.get('platforms', {}).get('macos', {})
excludes.extend(macos.get('exclude_source_files', []))
additional = macos.get('additional_source_files', [])

# Enumerate all source files (using glob order, NOT sorted - to match Python generator)
all_files = list(config.get('source_files', []))  # start with explicit source_files
for d in source_dirs:
    all_files.extend(glob.glob(f'{d}/*.c'))
    all_files.extend(glob.glob(f'{d}/*.cpp'))

# Remove excluded files
all_files = [f for f in all_files if f not in excludes]

# Add additional platform-specific files
all_files.extend(additional)

# Write cache
os.makedirs('temp', exist_ok=True)
with open('temp/source_files.json', 'w') as f:
    json.dump({"source_files": all_files}, f, indent=2)

print(f"Generated temp/source_files.json with {len(all_files)} files")
